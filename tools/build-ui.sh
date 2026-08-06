#!/usr/bin/env bash
# The build's presentation layer: colour, a banner, and a live progress bar over
# ninja's [n/m] output.
#
# It is a *filter*, never a decision maker -- every target still runs exactly the
# command it ran before, and the exit status is passed through untouched. If this
# script is removed the Makefile keeps working; the recipes only lose their
# decoration.
#
#   build-ui.sh banner [--full] [subtitle...]   engine banner / wordmark
#   build-ui.sh step|ok|warn|err|note MSG       one log line
#   build-ui.sh rule [TITLE]                    horizontal rule
#   build-ui.sh run LABEL -- CMD...             CMD with a progress bar
#   build-ui.sh helpfmt                         colourise `make help` on stdin
#
# Colour and the bar turn themselves off when stdout is not a terminal, when
# TERM is dumb, when NO_COLOR is set, or with PLAIN=1 (RAVEN_BUILD_PLAIN). CI
# logs therefore stay plain text without anyone configuring anything.
set -uo pipefail
shopt -s lastpipe 2>/dev/null || true

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
# Named after what they mean here, not after the colour, so the theme can be
# retuned in one place.
if (( plain )); then
	C_OFF= C_BOLD= C_DIM= C_ENGINE= C_PHASE= C_TARGET= C_VALUE= C_OK= C_WARN= C_ERR= C_BAR= C_TRACK=
elif (( ncolors >= 256 )); then
	C_OFF=$'\033[0m'; C_BOLD=$'\033[1m'; C_DIM=$'\033[38;5;244m'
	C_ENGINE=$'\033[1;38;5;141m'   # the raven: violet
	C_PHASE=$'\033[38;5;146m'      # what the build is doing now
	C_TARGET=$'\033[1;38;5;80m'    # target names, paths
	C_VALUE=$'\033[38;5;214m'      # numbers, variables, options
	C_OK=$'\033[38;5;114m'
	C_WARN=$'\033[38;5;179m'
	C_ERR=$'\033[1;38;5;203m'
	C_BAR=$'\033[38;5;141m'
	C_TRACK=$'\033[38;5;238m'
else
	C_OFF=$'\033[0m'; C_BOLD=$'\033[1m'; C_DIM=$'\033[2m'
	C_ENGINE=$'\033[1;35m'; C_PHASE=$'\033[35m'; C_TARGET=$'\033[1;36m'
	C_VALUE=$'\033[33m'; C_OK=$'\033[32m'; C_WARN=$'\033[33m'; C_ERR=$'\033[1;31m'
	C_BAR=$'\033[35m'; C_TRACK=$'\033[2m'
fi

# ---- glyphs -----------------------------------------------------------------
if (( utf8 )); then
	G_MARK='✦' G_STEP='▸' G_OK='✔' G_WARN='▲' G_ERR='✖' G_NOTE='·'
	G_FILL='█' G_HALF='▌' G_EMPTY='─' G_LCAP='▕' G_RCAP='▏' G_RULE='─'
	SPIN=('⠋' '⠙' '⠹' '⠸' '⠼' '⠴' '⠦' '⠧' '⠇' '⠏')
else
	G_MARK='*' G_STEP='>' G_OK='OK' G_WARN='!' G_ERR='X' G_NOTE='-'
	G_FILL='#' G_HALF='=' G_EMPTY='.' G_LCAP='[' G_RCAP=']' G_RULE='-'
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
		local head=${line:0:2}
		printf '%s%s %s%s%s %s%s\n' "$C_TRACK" "$head" \
			"$C_PHASE$C_BOLD" "$title" "$C_OFF" \
			"$C_TRACK${line:0:$(( w - ${#title} - 4 < 0 ? 0 : w - ${#title} - 4 ))}" "$C_OFF"
	else
		printf '%s%s%s\n' "$C_TRACK" "$line" "$C_OFF"
	fi
}

# ---- banner -----------------------------------------------------------------
# One line for a build (it prints on every invocation, so it stays small); the
# wordmark only where it was asked for, which is `make help`.
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
	printf '%s%s %s%s%s' "$C_ENGINE" "$G_MARK" "raven" "$C_OFF" ""
	printf ' %s%s%s' "$C_DIM" "$G_NOTE" "$C_OFF"
	printf ' %s%s%s\n' "$C_DIM" "$*" "$C_OFF"
}

# ---- diagnostics ------------------------------------------------------------
# Compiler output is highlighted only when it carries no escapes of its own, so
# a toolchain that already colours its diagnostics is left alone.
paint_line() {
	local l=$1
	if (( plain )) || [[ $l == *$'\033'* ]]; then printf '%s\n' "$l"; return; fi
	case $l in
		*"FAILED:"*|*" error:"*|*"error :"*|*"Error "*|*"undefined reference"*)
			printf '%s%s%s\n' "$C_ERR" "$l" "$C_OFF" ;;
		*" warning:"*|*"warning:"*)
			printf '%s%s%s\n' "$C_WARN" "$l" "$C_OFF" ;;
		*" note:"*|*"required from"*|*"In file included from"*|*"In function"*|*"In instantiation"*)
			printf '%s%s%s\n' "$C_DIM" "$l" "$C_OFF" ;;
		ninja:*|make*[Ee]rror*)
			printf '%s%s%s\n' "$C_VALUE" "$l" "$C_OFF" ;;
		*) printf '%s\n' "$l" ;;
	esac
}

# ---- the progress bar -------------------------------------------------------
bar_line() { # bar_line DONE TOTAL ELAPSED WHAT
	local done=$1 total=$2 elapsed=$3 what=$4
	local pct=0; (( total > 0 )) && pct=$(( done * 100 / total ))
	(( pct > 100 )) && pct=100

	local eta=''
	if (( total > 0 && done > 2 && pct > 0 && pct < 100 )); then
		eta=" $(hms $(( elapsed * (total - done) / done )))$(printf ' left')"
	fi

	local width=$(( TERM_COLS - 46 ))
	(( width > 28 )) && width=28
	(( width < 6 )) && width=6
	local filled=$(( total > 0 ? done * width / total : 0 ))
	(( filled > width )) && filled=$width

	local f e
	printf -v f '%*s' "$filled" ''; f=${f// /$G_FILL}
	printf -v e '%*s' $(( width - filled )) ''; e=${e// /$G_EMPTY}

	local head; head=$(printf '%s%s %s%s%s%s%s%s %s%3d%%%s %s%d/%d%s' \
		"$C_BAR" "${SPIN[$(( done % ${#SPIN[@]} ))]}" \
		"$C_TRACK$G_LCAP" "$C_BAR$f" "$C_TRACK$e$G_RCAP" "$C_OFF" '' '' \
		"$C_BOLD$C_VALUE" "$pct" "$C_OFF" \
		"$C_DIM" "$done" "$total" "$C_OFF")

	# Visible width of the head, escapes excluded, so the tail can be clipped to
	# what is actually left of the terminal.
	local plainhead="${SPIN[0]} ${G_LCAP}${f}${e}${G_RCAP}  $(printf '%3d%%' "$pct") $done/$total"
	local room=$(( TERM_COLS - ${#plainhead} - ${#eta} - 12 ))
	(( room < 0 )) && room=0
	printf '%s %s%s%s%s%s%s' "$head" \
		"$C_PHASE" "$(clip "$what" "$room")" "$C_OFF" \
		"$C_DIM" "$eta" "$C_OFF"
}

cmd_run() {
	local label=$1; shift
	[[ ${1:-} == -- ]] && shift
	(( $# )) || { cmd_err "run: no command"; return 2; }

	local logdir=${RAVEN_BUILD_LOG_DIR:-}
	local log=''
	if [[ -n $logdir ]]; then
		mkdir -p "$logdir" 2>/dev/null && log=$logdir/last-build.log
	fi

	local start=$SECONDS done=0 total=0 what=$label saw_progress=0 lines=0
	local hide='' show=''
	if (( ! plain )); then hide=$'\033[?25l'; show=$'\033[?25h'; fi
	local clear_eol=''; (( plain )) || clear_eol=$'\r\033[K'

	cleanup() { printf '%s%s' "$clear_eol" "$show"; }
	trap 'cleanup; exit 130' INT
	printf '%s' "$hide"

	# ninja's own status format is what the bar parses, so pin it: a user's
	# NINJA_STATUS would otherwise silently disable progress reporting.
	local status_fmt='[%f/%t] '

	{
		NINJA_STATUS=$status_fmt "$@" 2>&1
		echo "__RAVEN_UI_STATUS__ $?"
	} | {
		local last_render=-1
		while :; do
			local line rc
			IFS= read -r -t 1 line; rc=$?
			if (( rc > 128 )); then      # 1s tick: keep the clock and spinner alive
				(( saw_progress )) && { printf '%s' "$clear_eol"; bar_line "$done" "$total" "$(( SECONDS - start ))" "$what"; }
				continue
			fi
			(( rc != 0 )) && break       # EOF

			if [[ $line == __RAVEN_UI_STATUS__* ]]; then status=${line##* }; continue; fi
			[[ -n $log ]] && printf '%s\n' "$line" >> "$log"
			(( lines++ ))

			# [12/345] Building CXX object ...     (ninja)
			# [ 42%] Building CXX object ...       (make generator)
			if [[ $line =~ ^\[[[:space:]]*([0-9]+)/([0-9]+)\][[:space:]]*(.*)$ ]]; then
				done=${BASH_REMATCH[1]}; total=${BASH_REMATCH[2]}; what=${BASH_REMATCH[3]}
				saw_progress=1
			elif [[ $line =~ ^\[[[:space:]]*([0-9]+)%\][[:space:]]*(.*)$ ]]; then
				done=${BASH_REMATCH[1]}; total=100; what=${BASH_REMATCH[2]}
				saw_progress=1
			else
				printf '%s' "$clear_eol"
				paint_line "$line"
				(( saw_progress )) && bar_line "$done" "$total" "$(( SECONDS - start ))" "$what"
				continue
			fi

			if (( plain )); then
				# No bar without a terminal: one line per hundredth, so a CI log
				# shows progress without carrying thousands of build lines.
				local pct=$(( total > 0 ? done * 100 / total : 0 ))
				if (( pct != last_render )); then
					last_render=$pct
					printf '  %3d%%  %d/%d  %s\n' "$pct" "$done" "$total" "$what"
				fi
			else
				printf '%s' "$clear_eol"
				bar_line "$done" "$total" "$(( SECONDS - start ))" "$what"
			fi
		done

		printf '%s%s' "$clear_eol" "$show"
		local elapsed=$(( SECONDS - start ))
		if [[ ${status:-1} == 0 ]]; then
			if (( saw_progress && done > 0 )); then
				cmd_ok "$label $(printf '%s' "$C_DIM")$G_NOTE ${done} steps in $(hms "$elapsed")$C_OFF"
			elif (( lines <= 2 )); then
				cmd_ok "$label $(printf '%s' "$C_DIM")$G_NOTE already up to date$C_OFF"
			else
				cmd_ok "$label $(printf '%s' "$C_DIM")$G_NOTE $(hms "$elapsed")$C_OFF"
			fi
		else
			cmd_err "$label failed after $(hms "$elapsed") (exit ${status:-?})"
			[[ -n $log ]] && cmd_note "full output: $log"
		fi
		exit "${status:-1}"
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
				# "  make run            build + run the game"
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
