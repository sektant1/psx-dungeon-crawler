#!/usr/bin/env bash
# What is wrong with this build tree, and what to do about it.
#
#   make doctor            report
#   make doctor FIX=1      report, and repair what is safely repairable
#
# Every check here exists because the failure it looks for cost real time in
# this repository and none of them announce themselves: a build that is slow
# because ccache is silently bypassed looks exactly like a build that is slow.
# The repairs are limited to things that are caches or generated files -- a
# truncated dependency log, a stale symlink, a re-run of cmake. Nothing here
# deletes object files or source.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
ROOT=$PWD
UI=$ROOT/tools/build-ui.sh
BUILD_DIR=${BUILD_DIR:-build}
FIX=${FIX:-}

ok()   { "$UI" ok   "$@"; }
warn() { "$UI" warn "$@"; PROBLEMS=$((PROBLEMS + 1)); }
bad()  { "$UI" err  "$@"; PROBLEMS=$((PROBLEMS + 1)); }
note() { "$UI" note "$@"; }
sect() { "$UI" rule "$@"; }
# Advice attached to a warning: what the reader should do, indented under it.
tip()  { "$UI" note "  $*"; }

PROBLEMS=0
FIXED=0
fixed() { "$UI" ok "  fixed: $*"; FIXED=$((FIXED + 1)); }

human_mb() { awk -v kb="$1" 'BEGIN{ printf (kb>1048576 ? "%.1f GB" : "%d MB"), (kb>1048576 ? kb/1048576 : kb/1024) }'; }

"$UI" banner --full "build doctor$([ -n "$FIX" ] && echo " (FIX=1)")"

# ---- toolchain --------------------------------------------------------------
sect "toolchain"
for tool in cmake ninja ccache; do
	if path=$(command -v "$tool" 2>/dev/null); then
		version=$("$tool" --version 2>/dev/null | head -1)
		ok "$tool  ${version}"
	elif [[ $tool == ccache ]]; then
		warn "ccache not installed"
		tip "rebuilds recompile everything; install it and re-run 'make'"
	else
		bad "$tool not installed"
		tip "run 'make deps'"
	fi
done
command -v flock >/dev/null 2>&1 || {
	warn "flock not installed -- concurrent builds can corrupt the ninja deps log"
	tip "install util-linux; the Makefile takes a per-tree lock when it is present"
}

compiler=${CXX:-c++}
if command -v "$compiler" >/dev/null 2>&1; then
	ok "$compiler  $("$compiler" --version 2>/dev/null | head -1)"
else
	bad "no C++ compiler on PATH"
fi

for linker in mold ld.lld; do
	command -v "$linker" >/dev/null 2>&1 && { ok "$linker  (fast link)"; break; }
done

# The pretty renderer is optional by design; say which half is in use.
if "$ROOT/tools/build-ui.sh" --which-renderer 2>/dev/null | grep -q python; then
	ok "build UI: python + rich"
else
	note "build UI: bash (plain bars). 'make ui-deps' installs rich into .cache/py"
fi

# ---- build tree -------------------------------------------------------------
sect "build tree  ($BUILD_DIR)"
if [[ ! -f $BUILD_DIR/CMakeCache.txt ]]; then
	note "$BUILD_DIR is not configured yet -- 'make' will create it"
else
	generator=$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")
	buildtype=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$BUILD_DIR/CMakeCache.txt")
	ok "generator $generator, build type ${buildtype:-<none>}"

	# The ninja dependency log. Truncated, it makes an untouched tree rebuild
	# from scratch on every invocation, forever, and it never recovers on its
	# own -- each build's records land after the corruption and are dropped on
	# the next load. It looks exactly like a slow build.
	if [[ -f $BUILD_DIR/.ninja_deps ]]; then
		if ninja -C "$BUILD_DIR" -t deps 2>&1 >/dev/null | grep -q "premature end of file"; then
			bad "ninja dependency log is truncated -- every build is a full rebuild"
			if [[ -n $FIX ]]; then
				rm -f "$BUILD_DIR/.ninja_deps" "$BUILD_DIR/.ninja_log"
				fixed "cleared the deps log; the next build is a full one, then incremental"
			else
				tip "run 'make doctor FIX=1' or 'make build-reset' (costs one rebuild)"
			fi
		else
			ok "ninja dependency log intact ($(human_mb $(( $(stat -c%s "$BUILD_DIR/.ninja_deps") / 1024 ))))"
		fi
	fi

	# What an untouched tree still wants to do. Nonzero is normal right after an
	# edit; hundreds of steps with nothing edited is the symptom above.
	if [[ -f $BUILD_DIR/build.ninja ]]; then
		pending=$(ninja -C "$BUILD_DIR" -n game 2>/dev/null | grep -c '^\[' )
		if (( pending > 200 )); then
			warn "an incremental 'make' would run $pending steps"
			tip "if nothing was edited, that is the deps log above"
		else
			ok "incremental build would run $pending step(s)"
		fi
	fi
fi

# ---- ccache -----------------------------------------------------------------
sect "ccache"
if ! command -v ccache >/dev/null 2>&1; then
	note "not installed; skipping"
elif [[ ! -f $BUILD_DIR/CMakeFiles/rules.ninja ]]; then
	note "no generated rules to inspect yet"
else
	rules=$BUILD_DIR/CMakeFiles/rules.ninja
	# Is our launcher wired in at all?
	if grep -q 'LAUNCHER' "$rules"; then
		ok "compiler launcher present in the generated rules"
	else
		warn "no compiler launcher in the generated rules -- ccache is not being used"
		tip "reconfigure: cmake -S . -B $BUILD_DIR"
	fi

	# A *second* ccache in front of the launcher. A dependency that sets a
	# GLOBAL RULE_LAUNCH_COMPILE produces
	#     ccache  cmake -E env CCACHE_SLOPPINESS=... ccache  c++ -c foo.cpp
	# where the outer one takes `cmake` for the compiler, bails out with
	# "multiple source files" and runs the line uncached, and the inner one
	# reports "disabled" because it is nested. Nothing is cached and nothing
	# says so. cmake/Dependencies.cmake clears the property; this catches a
	# tree generated before that fix, or the next dependency to try it.
	doubled=$(grep -c '^ *command = [^ ]*ccache ' "$rules")
	if (( doubled > 0 )); then
		bad "$doubled rule(s) run ccache twice -- caching is disabled for them"
		tip "a dependency set a GLOBAL RULE_LAUNCH_COMPILE; regenerating clears it"
		if [[ -n $FIX ]]; then
			if cmake -S "$ROOT" -B "$BUILD_DIR" >/dev/null 2>&1; then
				fixed "regenerated the build system"
			else
				bad "  cmake regeneration failed; run it by hand to see why"
			fi
		else
			tip "run 'make doctor FIX=1'"
		fi
	else
		ok "one compiler launcher per rule"
	fi

	# Lifetime counters. They cover every build since the last --zero-stats, so
	# they are history, not a verdict on the current tree -- the rule check
	# above is the verdict.
	stats=$(ccache --print-stats 2>/dev/null)
	get() { awk -v k="$1" -F'\t' '$1==k{print $2}' <<<"$stats"; }
	hits=$(( $(get direct_cache_hit) + $(get preprocessed_cache_hit) ))
	misses=$(get cache_miss)
	bypassed=$(( $(get multiple_source_files) + $(get could_not_use_precompiled_header) ))
	total=$(( hits + misses ))
	if (( total > 0 )); then
		ok "lifetime: $hits/$total hits ($(( hits * 100 / total ))%)"
	fi
	if (( bypassed > 0 )); then
		note "lifetime: $bypassed call(s) bypassed the cache entirely"
		tip "if the rule check above is clean this is history: 'ccache --zero-stats' to re-measure"
	fi
	size_kb=$(get cache_size_kibibyte)
	max=$(ccache -p 2>/dev/null | sed -n 's/.*max_size = //p' | head -1)
	ok "cache $(human_mb "${size_kb:-0}") of ${max:-unknown}"
fi

# ---- capacity ---------------------------------------------------------------
sect "capacity"
mem_kb=$(awk '/MemAvailable/{print $2}' /proc/meminfo 2>/dev/null || echo 0)
cores=$(nproc 2>/dev/null || echo 1)
jobs=$(( mem_kb / 1024 / 1800 ))
(( jobs < 1 )) && jobs=1
(( jobs > cores )) && jobs=$cores
ok "$(human_mb "$mem_kb") available, $cores cores -> JOBS=$jobs"
if (( mem_kb / 1024 < 3600 )); then
	warn "little memory headroom; a parallel build can die as 'internal compiler error: Bus error'"
	tip "that is memory pressure, not a corrupt tree -- build with JOBS=1"
fi
free_kb=$(df -Pk "$BUILD_DIR" 2>/dev/null | awk 'NR==2{print $4}')
[[ -n ${free_kb:-} ]] && {
	if (( free_kb / 1024 / 1024 < 5 )); then
		warn "only $(human_mb "$free_kb") free on the build filesystem"
	else
		ok "$(human_mb "$free_kb") free on the build filesystem"
	fi
}
for stale in build-debug build-asan; do
	[[ -d $stale ]] && note "$stale exists ($(du -sh "$stale" 2>/dev/null | cut -f1)); 'make clean' removes it"
done

# ---- editor integration -----------------------------------------------------
sect "tooling"
if [[ -f $BUILD_DIR/compile_commands.json ]]; then
	if [[ -e compile_commands.json ]]; then
		ok "compile_commands.json linked at the repo root (clangd)"
	else
		warn "compile_commands.json exists only in $BUILD_DIR; clangd looks at the root"
		if [[ -n $FIX ]]; then
			ln -sf "$BUILD_DIR/compile_commands.json" compile_commands.json &&
				fixed "linked compile_commands.json into the repo root"
		else
			tip "run 'make doctor FIX=1'"
		fi
	fi
else
	note "no compile_commands.json yet -- configure the tree first"
fi

# ---- verdict ----------------------------------------------------------------
echo
if (( PROBLEMS == 0 )); then
	ok "nothing to fix"
elif (( FIXED > 0 )); then
	"$UI" ok "$FIXED repaired, $(( PROBLEMS - FIXED )) left to look at"
else
	"$UI" warn "$PROBLEMS thing(s) to look at$([ -z "$FIX" ] && echo "; 'make doctor FIX=1' repairs the safe ones")"
fi
exit 0
