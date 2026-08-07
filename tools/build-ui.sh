#!/usr/bin/env bash
# The build's presentation layer: colour, a banner, a progress bar over ninja's
# [n/m] output, and a digest of the compiler diagnostics instead of 400 lines of
# them.
#
# This is what the Makefile recipes call. If a python3 with `rich` is available
# it hands the work to tools/build_ui.py, which renders the same information
# with a nicer bar and a real table; otherwise it renders everything itself in
# bash. Bash is the floor on purpose -- building the game must not depend on a
# python package, and the machine missing it is usually the one already broken.
#
#   make ui-deps      installs rich into .cache/py (repo-local)
#
# It is a *filter*, never a decision maker -- every target still runs exactly the
# command it ran before, and the exit status is passed through untouched.
#
#   build-ui.sh banner [--full] [subtitle...]   engine banner / wordmark
#   build-ui.sh step|ok|warn|err|note MSG       one log line
#   build-ui.sh rule [TITLE]                    horizontal rule
#   build-ui.sh run LABEL -- CMD...             CMD with a progress bar
#   build-ui.sh helpfmt                         colourise `make help` on stdin
#   build-ui.sh --which-renderer                'python' or 'bash'
#
# Colour and the bar turn themselves off when stdout is not a terminal, when
# TERM is dumb, when NO_COLOR is set, or with PLAIN=1 (RAVEN_BUILD_PLAIN). CI
# logs therefore stay plain text without anyone configuring anything.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

# ---- renderer selection -----------------------------------------------------
# The repo-local venv first, then whatever python3 is on PATH; either only
# counts if it can actually import rich.
pick_python() {
	local candidate
	for candidate in "$ROOT/.cache/py/bin/python" python3; do
		command -v "$candidate" >/dev/null 2>&1 || continue
		"$candidate" -c 'import rich' 2>/dev/null && { echo "$candidate"; return 0; }
	done
	return 1
}
RICH_PYTHON=${RAVEN_BUILD_PYTHON-$(pick_python || true)}

if [[ ${1:-} == --which-renderer ]]; then
	[[ -n $RICH_PYTHON ]] && echo "python ($RICH_PYTHON)" || echo "bash"
	exit 0
fi
# RAVEN_BUILD_FORCE_BASH=1 renders in bash even when rich is installed -- for
# testing this half, which is otherwise only exercised on machines without it.
if [[ -n $RICH_PYTHON && ${RAVEN_BUILD_FORCE_BASH:-0} == 0 ]]; then
	exec "$RICH_PYTHON" "$ROOT/tools/build_ui.py" "$@"
fi

# ---- capability detection ---------------------------------------------------
is_tty=0; [[ -t 1 ]] && is_tty=1
plain=0
(( is_tty )) || plain=1
[[ -n ${NO_COLOR:-} ]] && plain=1
[[ ${RAVEN_BUILD_PLAIN:-0} != 0 && ${RAVEN_BUILD_PLAIN:-0} != false ]] && plain=1
case ${TERM:-dumb} in dumb|'') plain=1 ;; esac

utf8=0
case ${LC_ALL:-${LC_CTYPE:-${LANG:-}}} in *UTF-8*|*utf8*|*UTF8*|*utf-8*) utf8=1 ;; esac
(( plain )) && utf8=0

ncolors=$(tput colors 2>/dev/null || echo 0)

# ---- palette ----------------------------------------------------------------
# Grey and red, matching tools/build_ui.py. Named after meaning, not colour.
#   ember 167  the engine, and any number
#   blood 124  the filled part of a bar
#   rust  131  a warning
#   bone  253  names: targets, files, what is being built
#   ash   245  narration
#   iron  238  rules, bar track, structure
if (( plain )); then
	C_OFF= C_BOLD= C_DIM= C_ENGINE= C_PHASE= C_TARGET= C_VALUE= C_OK= C_WARN= C_ERR= C_BAR= C_TRACK=
elif (( ncolors >= 256 )); then
	C_OFF=$'\033[0m'; C_BOLD=$'\033[1m'; C_DIM=$'\033[38;5;242m'
	C_ENGINE=$'\033[1;38;5;167m'
	C_PHASE=$'\033[38;5;245m'
	C_TARGET=$'\033[1;38;5;253m'
	C_VALUE=$'\033[38;5;167m'
	C_OK=$'\033[38;5;252m'
	C_WARN=$'\033[38;5;131m'
	C_ERR=$'\033[1;38;5;196m'
	C_BAR=$'\033[38;5;124m'
	C_TRACK=$'\033[38;5;238m'
else
	C_OFF=$'\033[0m'; C_BOLD=$'\033[1m'; C_DIM=$'\033[2m'
	C_ENGINE=$'\033[1;31m'; C_PHASE=$'\033[37m'; C_TARGET=$'\033[1;37m'
	C_VALUE=$'\033[31m'; C_OK=$'\033[37m'; C_WARN=$'\033[31m'; C_ERR=$'\033[1;31m'
	C_BAR=$'\033[31m'; C_TRACK=$'\033[2m'
fi

# ---- glyphs -----------------------------------------------------------------
if (( utf8 )); then
	G_MARK='✦' G_STEP='▸' G_OK='✔' G_WARN='▲' G_ERR='✖' G_NOTE='·'
	# A full-height track (░) rather than a rule (─): the bar then reads as one
	# thick band, and matches what build_ui.py's ThickBar draws.
	G_FILL='█' G_EMPTY='░' G_LCAP='' G_RCAP='' G_RULE='─'
	SPIN=('⠋' '⠙' '⠹' '⠸' '⠼' '⠴' '⠦' '⠧' '⠇' '⠏')
else
	G_MARK='*' G_STEP='>' G_OK='OK' G_WARN='!' G_ERR='X' G_NOTE='-'
	G_FILL='#' G_EMPTY='.' G_LCAP='[' G_RCAP=']' G_RULE='-'
	SPIN=('|' '/' '-' '\')
fi

cols() { local c; c=$(tput cols 2>/dev/null) || c=0; (( c > 20 )) || c=100; echo "$c"; }
TERM_COLS=$(cols)
trap 'TERM_COLS=$(cols)' WINCH

# Truncate to a column count, appending an ellipsis. Byte-oriented, which is
# close enough for the ASCII paths ninja prints.
clip() {
	local s=$1 n=$2
	(( ${#s} <= n )) && { printf '%s' "$s"; return; }
	(( n <= 1 )) && { printf '%.1s' "$s"; return; }
	printf '%s%s' "${s:0:n-1}" "$( (( utf8 )) && printf '…' || printf '~' )"
}

hms() {
	local t=$1
	if (( t >= 3600 )); then printf '%dh%02dm' $((t/3600)) $(((t%3600)/60))
	elif (( t >= 60 )); then printf '%dm%02ds' $((t/60)) $((t%60))
	else printf '%ds' "$t"; fi
}

# ---- log lines --------------------------------------------------------------
say() { # say GLYPH COLOUR MSG...
	local g=$1 c=$2; shift 2
	printf '%s %s%s%s\n' "${c}${g}${C_OFF}" "$c" "$*" "$C_OFF"
}
cmd_step() { say "$G_STEP" "$C_PHASE" "$@"; }
cmd_ok()   { say "$G_OK"   "$C_OK"    "$@"; }
cmd_warn() { say "$G_WARN" "$C_WARN"  "$@"; }
cmd_err()  { say "$G_ERR"  "$C_ERR"   "$@" >&2; }
cmd_note() { say "$G_NOTE" "$C_DIM"   "$@"; }

cmd_rule() {
	local title=${*:-} w=$(( TERM_COLS > 78 ? 78 : TERM_COLS ))
	local line; printf -v line '%*s' "$w" ''; line=${line// /$G_RULE}
	if [[ -n $title ]]; then
		local rest=$(( w - ${#title} - 4 )); (( rest < 0 )) && rest=0
		printf '%s%s %s%s%s %s%s\n' "$C_TRACK" "${line:0:2}" \
			"$C_PHASE" "$title" "$C_OFF" "$C_TRACK${line:0:$rest}" "$C_OFF"
	else
		printf '%s%s%s\n' "$C_TRACK" "$line" "$C_OFF"
	fi
}

# ---- banner -----------------------------------------------------------------
# One line for a build (it prints on every invocation, so it stays small); the
# wordmark only where it was asked for, which is `make help` and `make doctor`.
wordmark() {
	if (( utf8 )); then
		printf '%s\n' \
			"${C_ENGINE}  █▀▄ ▄▀█ █ █ █▀▀ █▄ █${C_OFF}" \
			"${C_ENGINE}  █▀▄ █▀█ ▀▄▀ ██▄ █ ▀█${C_OFF}   ${C_DIM}a psx dungeon crawler engine${C_OFF}"
	else
		printf '%s\n' "${C_ENGINE}  R A V E N   E N G I N E${C_OFF}"
	fi
}

cmd_banner() {
	local full=0
	[[ ${1:-} == --full ]] && { full=1; shift; }
	if (( full )); then
		echo; wordmark; echo
		[[ $# -gt 0 ]] && cmd_note "$*"
		return
	fi
	printf '%s%s raven%s %s%s%s %s%s%s\n' \
		"$C_ENGINE" "$G_MARK" "$C_OFF" "$C_DIM" "$G_NOTE" "$C_OFF" \
		"$C_DIM" "$*" "$C_OFF"
}

# ---- the progress bar -------------------------------------------------------
bar_line() { # bar_line DONE TOTAL ELAPSED WHAT
	local done=$1 total=$2 elapsed=$3 what=$4
	local pct=0; (( total > 0 )) && pct=$(( done * 100 / total ))
	(( pct > 100 )) && pct=100

	local eta=''
	if (( total > 0 && done > 2 && pct > 0 && pct < 100 )); then
		eta=" $(hms $(( elapsed * (total - done) / done ))) left"
	fi

	local width=$(( TERM_COLS - 46 ))
	(( width > 28 )) && width=28
	(( width < 6 )) && width=6
	local filled=$(( total > 0 ? done * width / total : 0 ))
	(( filled > width )) && filled=$width

	local f e
	printf -v f '%*s' "$filled" ''; f=${f// /$G_FILL}
	printf -v e '%*s' $(( width - filled )) ''; e=${e// /$G_EMPTY}

	# The visible text, escapes excluded, so the tail can be clipped to what is
	# actually left of the terminal.
	local bare="${SPIN[0]} ${G_LCAP}${f}${e}${G_RCAP} $(printf '%3d%%' "$pct") $done/$total"
	local room=$(( TERM_COLS - ${#bare} - ${#eta} - 4 ))
	(( room < 0 )) && room=0

	printf '%s%s%s %s%s%s%s%s%s%s %s%3d%%%s %s%d/%d%s %s%s%s%s%s%s' \
		"$C_BAR" "${SPIN[$(( done % ${#SPIN[@]} ))]}" "$C_OFF" \
		"$C_TRACK$G_LCAP" "$C_BAR" "$f" "$C_TRACK" "$e" "$G_RCAP" "$C_OFF" \
		"$C_BOLD$C_VALUE" "$pct" "$C_OFF" \
		"$C_DIM" "$done" "$total" "$C_OFF" \
		"$C_PHASE" "$(clip "$what" "$room")" "$C_OFF" "$C_DIM" "$eta" "$C_OFF"
}

# ---- diagnostics ------------------------------------------------------------
# See the long note in build_ui.py: errors print in full, warnings collapse to
# one headline each (and are counted, not printed, when they come from a
# dependency nobody here can fix), and the untouched text is always in the log.
#
# `rel` shortens a path against the repo and names vendored trees by package
# rather than by content hash.
rel() {
	local p=$1
	case $p in
		*/.cache/cpm/*) printf '%s' "$(sed -E 's|.*/\.cache/cpm/([^/]+)/[0-9a-f]{7,}/|\1/|' <<<"$p")"; return ;;
		*/_deps/*)      printf '%s' "$(sed -E 's|.*/_deps/([^/]+?)-(src\|build)/|\1/|' <<<"$p")"; return ;;
		"$ROOT"/*)      printf '%s' "${p#"$ROOT"/}"; return ;;
	esac
	printf '%s' "$p"
}
is_vendor() {
	case $1 in
		*/.cache/cpm/*|*/_deps/*|/usr/*|*/third_party/*) return 0 ;;
		*) return 1 ;;
	esac
}

cmd_run() {
	local label=$1; shift
	[[ ${1:-} == -- ]] && shift
	(( $# )) || { cmd_err "run: no command"; return 2; }

	local log=''
	if [[ -n ${RAVEN_BUILD_LOG_DIR:-} ]]; then
		mkdir -p "$RAVEN_BUILD_LOG_DIR" 2>/dev/null && log=$RAVEN_BUILD_LOG_DIR/last-build.log
		[[ -n $log ]] && : > "$log"
	fi

	local hide='' show='' clear_eol=''
	if (( ! plain )); then hide=$'\033[?25l'; show=$'\033[?25h'; clear_eol=$'\r\033[K'; fi
	trap 'printf "%s%s" "$clear_eol" "$show"; exit 130' INT
	printf '%s' "$hide"

	# ninja's status format is what the bar parses, so pin it: a user's
	# NINJA_STATUS would otherwise silently disable progress reporting.
	{
		NINJA_STATUS='[%f/%t] ' "$@" 2>&1
		echo "__RAVEN_UI_STATUS__ $?"
	} | {
		local start=$SECONDS done=0 total=0 what=$label saw_progress=0 lines=0
		local status=1 last_pct=-1 swallow=0 errors=0 warnings=0 ours=0
		local -A flag_ours=() flag_deps=() flag_where=()

		repaint() {
			(( saw_progress && ! plain )) || return 0
			printf '%s' "$clear_eol"
			bar_line "$done" "$total" "$(( SECONDS - start ))" "$what"
		}
		emit() { printf '%s' "$clear_eol"; printf '%s\n' "$1"; repaint; }

		while :; do
			local line rc
			IFS= read -r -t 1 line; rc=$?
			if (( rc > 128 )); then repaint; continue; fi   # 1s tick: keep the clock moving
			(( rc != 0 )) && break                           # EOF

			if [[ $line == __RAVEN_UI_STATUS__* ]]; then status=${line##* }; continue; fi
			[[ -n $log ]] && printf '%s\n' "$line" >> "$log"
			(( lines++ ))

			# [12/345] ... (ninja)   or   [ 42%] ... (make generator)
			if [[ $line =~ ^\[[[:space:]]*([0-9]+)/([0-9]+)\][[:space:]]*(.*)$ ]]; then
				done=${BASH_REMATCH[1]}; total=${BASH_REMATCH[2]}; what=${BASH_REMATCH[3]}
				saw_progress=1; swallow=0
			elif [[ $line =~ ^\[[[:space:]]*([0-9]+)%\][[:space:]]*(.*)$ ]]; then
				done=${BASH_REMATCH[1]}; total=100; what=${BASH_REMATCH[2]}
				saw_progress=1; swallow=0
			# ctest: "  12/153 Test  #80: actions ......   Passed    0.01 sec"
			elif [[ $line =~ ^[[:space:]]*([0-9]+)/([0-9]+)[[:space:]]+Test[[:space:]]+#[0-9]+:[[:space:]]+([^[:space:]]+)[[:space:]]*\.*[[:space:]]*(.*)$ ]]; then
				done=${BASH_REMATCH[1]}; total=${BASH_REMATCH[2]}; what=${BASH_REMATCH[3]}
				saw_progress=1; swallow=0
				local verdict=${BASH_REMATCH[4]}
				if [[ -n $verdict && $verdict != Passed* ]]; then
					(( errors++ ))
					emit "${C_ERR}${G_ERR} ${what}${C_OFF}  ${C_DIM}${verdict}${C_OFF}"
				fi
			elif [[ $line =~ ^([^[:space:]:][^:]*):([0-9]+):([0-9]+)?:?[[:space:]]*(error|warning|note|fatal\ error):[[:space:]]*(.*)$ ]]; then
				local file=${BASH_REMATCH[1]} lno=${BASH_REMATCH[2]}
				local kind=${BASH_REMATCH[4]} msg=${BASH_REMATCH[5]}
				local flag='-W???'
				[[ $msg =~ \[(-W[A-Za-z0-9+_-]+)\]$ ]] && { flag=${BASH_REMATCH[1]}; msg=${msg%%\[-W*}; }
				case $kind in
					*error)
						(( errors++ )); swallow=0
						emit "${C_ERR}${G_ERR} $(rel "$file"):${lno}  ${msg}${C_OFF}" ;;
					note) swallow=1 ;;
					warning)
						(( warnings++ )); swallow=1
						if is_vendor "$file"; then
							flag_deps[$flag]=$(( ${flag_deps[$flag]:-0} + 1 ))
						else
							(( ours++ ))
							flag_ours[$flag]=$(( ${flag_ours[$flag]:-0} + 1 ))
							flag_where[$flag]=${flag_where[$flag]:-$(rel "$file"):$lno}
							emit "${C_WARN}${G_WARN} $(rel "$file"):${lno}${C_OFF}  ${C_DIM}${msg}${C_OFF} ${C_TRACK}${flag}${C_OFF}"
						fi ;;
				esac
				continue
			elif [[ $line == FAILED:* ]]; then
				swallow=0; emit "${C_ERR}${line}${C_OFF}"; continue
			elif (( swallow )) && [[ $line =~ ^[[:space:]]*([0-9]+[[:space:]]*\||\||\^|~|from|required\ from|In\ (file|function|member|instantiation|constructor|lambda|static)) ]]; then
				continue
			elif [[ -z ${line// } ]]; then
				continue
			else
				swallow=0
				case $line in
					*"undefined reference"*|ninja:*ERROR*|*"Error "*)
						emit "${C_ERR}${line}${C_OFF}" ;;
					ninja:*|cc1plus:*|make*)
						emit "${C_VALUE}${line}${C_OFF}" ;;
					*)  emit "${C_DIM}${line}${C_OFF}" ;;
				esac
				continue
			fi

			if (( plain )); then
				# No bar without a terminal: one line per hundredth, so a CI log
				# shows progress without carrying thousands of build lines.
				local pct=$(( total > 0 ? done * 100 / total : 0 ))
				if (( pct != last_pct )); then
					last_pct=$pct
					printf '  %3d%%  %d/%d  %s\n' "$pct" "$done" "$total" "$what"
				fi
			else
				repaint
			fi
		done

		printf '%s%s' "$clear_eol" "$show"

		# --- the closing report ---
		if (( warnings > 0 )); then
			echo
			cmd_rule "warnings"
			local flag
			for flag in $(printf '%s\n' "${!flag_ours[@]}" "${!flag_deps[@]}" | sort -u); do
				[[ -z $flag ]] && continue
				printf '  %s%-34s%s %s%4s%s %s%4s%s  %s%s%s\n' \
					"$C_WARN" "$flag" "$C_OFF" \
					"$C_VALUE" "${flag_ours[$flag]:-}" "$C_OFF" \
					"$C_TRACK" "${flag_deps[$flag]:+${flag_deps[$flag]} dep}" "$C_OFF" \
					"$C_DIM" "${flag_where[$flag]:-}" "$C_OFF"
			done
			[[ -n $log ]] && cmd_note "  full text: $log"
			echo
		fi

		local elapsed=$(( SECONDS - start ))
		local tail=''
		(( saw_progress && done > 0 )) && tail+=" $G_NOTE ${done} steps"
		tail+=" $G_NOTE $(hms "$elapsed")"
		(( warnings > 0 )) && tail+=" $G_NOTE ${warnings} warnings (${ours} ours)"

		if [[ $status == 0 ]]; then
			if (( ! saw_progress && lines <= 2 )); then
				cmd_ok "$label ${C_DIM}$G_NOTE already up to date${C_OFF}"
			else
				printf '%s%s %s%s%s%s%s\n' "$C_OK" "$G_OK" "$label" "$C_OFF" \
					"$C_DIM" "$tail" "$C_OFF"
			fi
		else
			cmd_err "$label failed after $(hms "$elapsed") (exit $status)"
			(( errors > 0 )) && cmd_note "$errors error(s) above"
			[[ -n $log ]] && cmd_note "full output: $log"
		fi
		exit "$status"
	}
	local rc=${PIPESTATUS[1]}
	trap - INT
	return "$rc"
}

# ---- `make help` ------------------------------------------------------------
cmd_helpfmt() {
	(( plain )) && { cat; return; }
	local first=1
	while IFS= read -r line; do
		if (( first )); then first=0; wordmark; continue; fi
		case $line in
			'') echo ;;
			[!\ ]*:)  printf '%s%s%s\n' "$C_BOLD$C_PHASE" "$line" "$C_OFF" ;;
			'  make '*)
				if [[ $line =~ ^(\ \ make\ [A-Za-z0-9_.-]+(\ [A-Z]+=)?)(.*)$ ]]; then
					printf '%s%s%s%s%s%s\n' "$C_TARGET" "${BASH_REMATCH[1]}" "$C_OFF" \
						"$C_DIM" "${BASH_REMATCH[3]}" "$C_OFF"
				else printf '%s\n' "$line"; fi ;;
			'  '[A-Z]*)
				if [[ $line =~ ^(\ \ [A-Z_]+=[^\ ]*)(.*)$ ]]; then
					printf '%s%s%s%s%s%s\n' "$C_VALUE" "${BASH_REMATCH[1]}" "$C_OFF" \
						"$C_DIM" "${BASH_REMATCH[2]}" "$C_OFF"
				else printf '%s%s%s\n' "$C_DIM" "$line" "$C_OFF"; fi ;;
			*) printf '%s%s%s\n' "$C_DIM" "$line" "$C_OFF" ;;
		esac
	done
}

# ---- entry ------------------------------------------------------------------
sub=${1:-}; shift 2>/dev/null || true
case $sub in
	banner)   cmd_banner "$@" ;;
	step)     cmd_step "$@" ;;
	ok)       cmd_ok "$@" ;;
	warn)     cmd_warn "$@" ;;
	err)      cmd_err "$@" ;;
	note)     cmd_note "$@" ;;
	rule)     cmd_rule "$@" ;;
	run)      cmd_run "$@" ;;
	helpfmt)  cmd_helpfmt ;;
	wordmark) wordmark ;;
	''|-h|--help)
		echo "usage: build-ui.sh {banner|step|ok|warn|err|note|rule|run|helpfmt} ..." ;;
	*) echo "build-ui.sh: unknown subcommand '$sub'" >&2; exit 2 ;;
esac
