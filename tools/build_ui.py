#!/usr/bin/env python3
"""The build's presentation layer -- the rich-powered half.

`tools/build-ui.sh` is what the Makefile recipes call. It hands over to this
script when a python3 with `rich` is importable and renders the same information
itself, in bash, when it is not. Keeping bash as the floor is deliberate:
building the game must not depend on a python package being installed, and the
machine where the pretty renderer is missing is usually the one where something
is already wrong.

    make ui-deps        put rich in .cache/py (repo-local, nothing system-wide)

The subcommands mirror build-ui.sh exactly, so either half can serve any recipe:

    build_ui.py banner [--full] [detail...]
    build_ui.py step|ok|warn|err|note MSG...
    build_ui.py rule [TITLE]
    build_ui.py run LABEL -- CMD...
    build_ui.py helpfmt < text

`run` is the interesting one: it drives CMD, turns ninja's `[n/m]` chatter into a
progress bar, *digests* compiler diagnostics rather than letting 98 warnings
scroll past (see the Diagnostics class), tees an uncoloured transcript to
$RAVEN_BUILD_LOG_DIR/last-build.log, reports what ccache did, and exits with
CMD's own status.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import time
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

try:
    from rich.console import Console, Group
    from rich.live import Live
    from rich.progress import (
        MofNCompleteColumn,
        Progress,
        ProgressColumn,
        SpinnerColumn,
        TaskProgressColumn,
        TextColumn,
        TimeElapsedColumn,
        TimeRemainingColumn,
    )
    from rich.table import Table
    from rich.text import Text
    from rich.theme import Theme

    HAVE_RICH = True
except ImportError:  # the bash half normally catches this before we are called
    HAVE_RICH = False


# --- theme -------------------------------------------------------------------
# Grey and red: the engine's own palette. Styles are named after meaning rather
# than colour, so the whole build can be retuned from this one dict.
#
#   ember  #d64045   the engine, and anything that is a number
#   blood  #a41623   the filled part of a progress bar
#   rust   #8c3b3b   a warning: present, not urgent
#   bone   #e6e2df   names -- targets, files, the thing being built
#   ash    #a8a29e   narration
#   iron   #3f3c3a   rules, bar track, everything structural
THEME = {
    "engine": "bold #d64045",
    "phase": "#a8a29e",
    "target": "bold #e6e2df",
    "value": "#d64045",
    "ok": "#d5d0cc",
    "warn": "#b05252",
    "err": "bold #e5484d",
    "muted": "#78716c",
    "faint": "#57534e",
    "rule": "#3f3c3a",
    "bar.complete": "#a41623",
    "bar.finished": "#d64045",
    "bar.pulse": "#8c3b3b",
    # rich ships its own colours for the progress columns -- magenta for the
    # percentage, yellow for elapsed, cyan for remaining. Left alone they put
    # three hues that belong to no part of this palette in the middle of the
    # bar, so every one is restated here as a step along the same grey-to-red
    # ramp: the percentage is the brightest (it is the number being read),
    # counts sit a shade back, and the two clocks are quieter still.
    "progress.percentage": "bold #d64045",
    "progress.download": "#a8a29e",     # the n/m counter
    "progress.elapsed": "#78716c",
    "progress.remaining": "#8c3b3b",
    "progress.spinner": "#8c3b3b",
    "progress.description": "#a8a29e",
    "progress.data.speed": "#a8a29e",
    "bar.back": "#3f3c3a",
}

# PLAIN=1/NO_COLOR is for logs and CI, so it drops the glyphs along with the
# colour -- a build log full of box-drawing characters is not plainer for
# having lost its escape codes.
PLAIN = bool(os.environ.get("NO_COLOR")) or os.environ.get(
    "RAVEN_BUILD_PLAIN"
) not in (None, "", "0", "false")

if PLAIN:
    MARK, STEP, OK, WARN, ERR, NOTE = "*", ">", "OK", "!", "X", "-"
    WORDMARK = ("  R A V E N   E N G I N E", "")
else:
    MARK, STEP, OK, WARN, ERR, NOTE = "✦", "▸", "✔", "▲", "✖", "·"
    WORDMARK = ("  █▀▄ ▄▀█ █ █ █▀▀ █▄ █", "  █▀▄ █▀█ ▀▄▀ ██▄ █ ▀█")


def console(stderr: bool = False) -> "Console":
    return Console(
        theme=Theme(THEME),
        stderr=stderr,
        no_color=PLAIN,
        highlight=False,
        soft_wrap=False,
    )


def print_wordmark(con: "Console") -> None:
    con.print(Text(WORDMARK[0], style="engine"))
    if WORDMARK[1]:
        con.print(
            Text.assemble(
                (WORDMARK[1], "engine"),
                ("   a psx dungeon crawler engine", "faint"),
            )
        )


# --- log lines ---------------------------------------------------------------
def _say(glyph: str, style: str, msg: str, stderr: bool = False) -> None:
    # Wrapped, not truncated: these lines carry paths and commands the reader is
    # meant to be able to copy.
    console(stderr).print(Text.assemble((f"{glyph} ", style), (msg, style)))


def cmd_step(a): _say(STEP, "phase", " ".join(a))
def cmd_ok(a): _say(OK, "ok", " ".join(a))
def cmd_warn(a): _say(WARN, "warn", " ".join(a))
def cmd_err(a): _say(ERR, "err", " ".join(a), stderr=True)
def cmd_note(a): _say(NOTE, "muted", " ".join(a))


def cmd_rule(a) -> None:
    title = " ".join(a)
    console().rule(
        Text(title, style="phase") if title else "", style="rule", align="left"
    )


def cmd_banner(a) -> None:
    con = console()
    full = bool(a) and a[0] == "--full"
    detail = " ".join(a[1:] if full else a)
    if full:
        con.print()
        print_wordmark(con)
        con.print()
        if detail:
            cmd_note([detail])
        return
    con.print(
        Text.assemble(
            (f"{MARK} raven", "engine"), (f" {NOTE} ", "faint"), (detail, "muted")
        )
    )


# --- diagnostics -------------------------------------------------------------
# The build emits ~100 warnings, three to five lines each: a headline, the
# offending source line, a caret, sometimes a chain of "required from" notes.
# Printed verbatim that is 400 lines of scrollback per build, most of it from
# dependencies nobody here can fix, and the two warnings that *are* new are
# invisible in it.
#
# So: errors print in full and immediately -- they are the reason you are
# reading. Warnings are collapsed to one headline each and tallied by kind, with
# dependency warnings kept out of the stream entirely and only counted. The
# untouched transcript always goes to last-build.log, so nothing is lost.

HEADLINE = re.compile(
    r"^(?P<file>[^\s:][^:]*):(?P<line>\d+)(?::(?P<col>\d+))?:\s+"
    r"(?P<kind>error|warning|note|fatal error):\s+(?P<msg>.*)$"
)
FLAG = re.compile(r"\s*\[(-W[\w+-]+)\]\s*$")
# The source echo and caret that GCC prints *under* a diagnostic:
#     491 |         #define PUGI_IMPL_GETPAGE(n) ...
#         |                                  ^~~~
CONTINUATION = re.compile(r"^\s*(\d+\s*\||\||\^|~)")
NINJA_FAILED = re.compile(r"^FAILED:\s")
# The context GCC prints *above* a diagnostic: the enclosing scope, the include
# chain, the template instantiation stack. Indispensable for an error, noise for
# a warning, and it arrives before either -- so it is buffered, then flushed or
# dropped once the diagnostic itself says which it was.
PREAMBLE = re.compile(
    r"^(In file included from\s)|(\s*(from|required from|inlined from)\s)|"
    r"^(\S.*:\s+)?(In|At)\s+(function|member function|constructor|destructor|"
    r"instantiation of|lambda function|static member function|substitution|"
    r"global scope|top level|copy constructor)\b"
)
# Where a diagnostic comes from decides whether it is actionable here.
VENDOR = re.compile(r"(/\.cache/cpm/)|(/_deps/)|(^/usr/)|(/third_party/)")


@dataclass
class Diagnostics:
    """Collects diagnostics during a build and renders the closing digest."""

    root: Path
    errors: int = 0
    # (flag, ours) -> count, and flag -> the files that raised it
    tally: Counter = field(default_factory=Counter)
    by_flag: dict[str, Counter] = field(default_factory=lambda: defaultdict(Counter))
    # One headline per (file, line, flag): the same header included from twenty
    # translation units warns twenty times about one line of code.
    seen: set = field(default_factory=set)

    def relative(self, path: str) -> str:
        """Shorten a path against the repo, and name vendored trees by package
        rather than by content hash -- `.cache/cpm/assimp/08046b0.../foo.cpp`
        says nothing that `assimp/foo.cpp` does not."""
        if (match := re.search(r"/\.cache/cpm/([^/]+)/[0-9a-f]{7,}/(.*)$", path)):
            return f"{match[1]}/{match[2]}"
        if (match := re.search(r"/_deps/([^/]+?)-(?:src|build)/(.*)$", path)):
            return f"{match[1]}/{match[2]}"
        try:
            return str(Path(path).resolve().relative_to(self.root))
        except (ValueError, OSError):
            return path

    def shorten(self, text: str) -> str:
        """`relative` applied to every path embedded in a line of prose -- what
        turns a three-deep include chain from three wrapped lines into three."""
        return re.sub(
            r"/[\w./+-]+\.(?:h|hpp|hxx|c|cc|cpp|cxx|inl|ipp)\b",
            lambda m: self.relative(m[0]),
            text,
        )

    def record(self, match: re.Match) -> "Text | None":
        """Note a diagnostic. Returns the line to print now, or None to keep it
        out of the stream and let the digest speak for it."""
        kind, path = match["kind"], match["file"]
        ours = not VENDOR.search(path)
        flag_match = FLAG.search(match["msg"])
        # GCC omits the flag for warnings that are always on (and for #warning).
        flag = flag_match[1] if flag_match else ("error" if "error" in kind else "(always on)")
        message = FLAG.sub("", match["msg"]).strip()
        where = f"{self.relative(path)}:{match['line']}"

        if "error" in kind:
            self.errors += 1
            return Text.assemble(
                (f"{ERR} ", "err"), (where, "err"), ("  ", ""), (message, "err")
            )

        if kind == "note":  # only ever context for something already reported
            return None

        self.tally[(flag, ours)] += 1
        self.by_flag[flag][self.relative(path)] += 1

        key = (where, flag)
        if key in self.seen:
            return None
        self.seen.add(key)
        if not ours:  # a dependency's warning: counted, never printed
            return None
        # The caller prints this with no_wrap: the digest carries the counts and
        # the log carries the full text, so a warning that runs to 300
        # characters of template spelling does not get to own the screen.
        return Text.assemble(
            (f"{WARN} ", "warn"),
            (where, "warn"),
            ("  ", ""),
            (message, "muted"),
            (f"  {flag}", "faint"),
        )

    @property
    def warnings(self) -> int:
        return sum(self.tally.values())

    def digest(self) -> "Table | None":
        """One table: what was warned about, how often, and how much of it is
        this repository's problem."""
        if not self.tally:
            return None
        table = Table(
            box=None, pad_edge=False, show_header=True, header_style="faint",
            padding=(0, 2, 0, 2), expand=True,
        )
        table.add_column("warning", style="warn", no_wrap=True)
        table.add_column("ours", style="value", justify="right", width=4)
        table.add_column("deps", style="faint", justify="right", width=4)
        table.add_column(
            "mostly from", style="muted", no_wrap=True, overflow="ellipsis", ratio=1
        )

        # Ours first -- those are the ones somebody here can act on -- then by
        # how loud each is.
        flags = sorted(
            {flag for flag, _ in self.tally},
            key=lambda f: (-self.tally[(f, True)], -self.tally[(f, False)]),
        )
        for flag in flags:
            ours, theirs = self.tally[(flag, True)], self.tally[(flag, False)]
            worst, count = self.by_flag[flag].most_common(1)[0]
            table.add_row(
                flag,
                str(ours) if ours else "",
                str(theirs) if theirs else "",
                f"{worst}{f'  x{count}' if count > 1 else ''}",
            )
        return table


# --- ccache ------------------------------------------------------------------
def ccache_stats() -> dict[str, int]:
    """ccache's machine-readable counters, or {} if ccache is not installed."""
    if not shutil.which("ccache"):
        return {}
    try:
        out = subprocess.run(
            ["ccache", "--print-stats"], capture_output=True, text=True, timeout=10
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return {}
    stats: dict[str, int] = {}
    for line in out.splitlines():
        key, _, value = line.partition("\t")
        try:
            stats[key.strip()] = int(value)
        except ValueError:
            pass
    return stats


def ccache_delta(before: dict[str, int], after: dict[str, int]) -> str | None:
    """What ccache did during this build, as one clause -- or None if it did
    nothing worth reporting (a link-only or already-up-to-date build)."""
    if not before or not after:
        return None

    def d(key: str) -> int:
        return after.get(key, 0) - before.get(key, 0)

    hits = d("direct_cache_hit") + d("preprocessed_cache_hit")
    misses = d("cache_miss")
    bypassed = d("multiple_source_files") + d("could_not_use_precompiled_header")
    total = hits + misses
    if total == 0 and bypassed == 0:
        return None
    parts = []
    if total:
        parts.append(f"ccache {hits}/{total} ({hits * 100 // total}%)")
    if bypassed:
        parts.append(f"{bypassed} uncached -- see `make doctor`")
    return ", ".join(parts)


# --- run ---------------------------------------------------------------------
NINJA_STEP = re.compile(r"^\[\s*(\d+)/(\d+)\]\s*(.*)$")   # ninja
MAKE_STEP = re.compile(r"^\[\s*(\d+)%\]\s*(.*)$")          # makefile generator
# ctest: "  12/153 Test  #80: actions ....................   Passed    0.01 sec"
# The same bar serves `make test`, so a 153-test suite reports rather than
# scrolls. A test that did not pass is pulled out and printed.
CTEST_STEP = re.compile(
    r"^\s*(\d+)/(\d+)\s+Test\s+#\d+:\s+(\S+)\s+\.*\s*(.*?)\s*$"
)
CTEST_PASSED = "Passed"
# ctest prints a test's captured output between these when it fails.
CTEST_FAILED_LINE = re.compile(r"^\s*\d+\s*-\s+\S+\s+\((Failed|Timeout|.*Exception.*)\)")


class ThickBar(ProgressColumn):
    """A full-height progress bar.

    rich's own BarColumn draws with `━`, a single rule through the middle of the
    cell, which reads as thin next to the block glyphs the rest of this output
    uses. This fills the whole cell instead -- `█` for done, `░` for the track --
    and keeps the eighth-block partials so the bar still advances smoothly
    rather than one whole character at a time. It matches what build-ui.sh
    draws in bash, so the two renderers look like one tool.
    """

    # Index 0 is unused: a partial that rounds to nothing must draw track, not a
    # blank cell, or the bar shows a hole at its own leading edge.
    PARTIALS = " ▏▎▍▌▋▊▉"
    TRACK = "░"

    def __init__(self, width: int = 34) -> None:
        super().__init__()
        self.width = width

    def render(self, task) -> "Text":
        if PLAIN:
            return Text("")
        if not task.total:  # total unknown: nothing honest to draw yet
            return Text(self.TRACK * self.width, style="bar.back")

        done = min(max(task.completed / task.total, 0.0), 1.0)
        cells = done * self.width
        full = int(cells)
        bar = Text("█" * full, style="bar.finished" if done >= 1 else "bar.complete")

        remaining = self.width - full
        if remaining > 0:
            index = int((cells - full) * len(self.PARTIALS))
            if index:
                bar.append(self.PARTIALS[index], style="bar.complete")
                remaining -= 1
            bar.append(self.TRACK * remaining, style="bar.back")
        return bar


def human(seconds: float) -> str:
    s = int(seconds)
    if s >= 3600:
        return f"{s // 3600}h{(s % 3600) // 60:02d}m"
    if s >= 60:
        return f"{s // 60}m{s % 60:02d}s"
    return f"{s}s"


def cmd_run(argv: list[str]) -> int:
    if not argv:
        cmd_err(["run: no label"])
        return 2
    label, rest = argv[0], argv[1:]
    if rest and rest[0] == "--":
        rest = rest[1:]
    if not rest:
        cmd_err(["run: no command"])
        return 2

    con = console()
    log_dir = os.environ.get("RAVEN_BUILD_LOG_DIR")
    log_path = Path(log_dir) / "last-build.log" if log_dir else None
    log = None
    if log_path:
        try:
            log_path.parent.mkdir(parents=True, exist_ok=True)
            log = open(log_path, "w", errors="replace")
        except OSError:
            log = None

    env = dict(os.environ)
    # Pin the status format the bar parses: a user's NINJA_STATUS would
    # otherwise silently turn progress reporting off.
    env["NINJA_STATUS"] = "[%f/%t] "

    diag = Diagnostics(root=Path(__file__).resolve().parent.parent)
    before = ccache_stats()
    started = time.monotonic()

    # Without a terminal there is nothing to animate: a spinner redrawn into a
    # log file is thousands of lines of escape codes. Report one line per
    # hundredth instead, which is what a CI log actually wants.
    live_ui = con.is_terminal

    progress = Progress(
        SpinnerColumn(style="bar.pulse"),
        TextColumn("[phase]{task.description}[/]"),
        ThickBar(),
        TaskProgressColumn(style="progress.percentage"),
        MofNCompleteColumn(),
        TimeElapsedColumn(),
        TimeRemainingColumn(compact=True),
        console=con,
        transient=True,
        # Not expand=True: the bar is a fixed width, so expanding the row only
        # pads the flexible column and paints a stripe of bar colour past the
        # bar's own end.
        expand=False,
        disable=not live_ui,
    )
    action = Text("", style="faint", no_wrap=True, overflow="ellipsis")
    task = progress.add_task(label, total=None)
    last_pct = -1

    steps = 0
    lines = 0
    # True while inside a diagnostic whose source echo and carets belong to
    # something already collapsed into the digest.
    swallow_continuation = False
    # Context lines seen since the last diagnostic, held until one arrives.
    preamble: list[str] = []

    try:
        proc = subprocess.Popen(
            rest,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            errors="replace",
            bufsize=1,
            env=env,
        )
    except OSError as exc:
        cmd_err([f"{label}: cannot run {rest[0]}: {exc}"])
        return 127

    with Live(
        Group(progress, action), console=con, refresh_per_second=12, transient=True,
        auto_refresh=live_ui,
    ) as live:
        def advance(done: int, total: int, what: str) -> None:
            nonlocal last_pct
            progress.update(task, completed=done, total=total)
            action.plain = "  " + what
            if live_ui:
                return
            pct = done * 100 // total if total else 0
            if pct != last_pct:
                last_pct = pct
                live.console.print(f"  {pct:3d}%  {done}/{total}  {what}", style="faint")

        assert proc.stdout is not None
        for raw in proc.stdout:
            line = raw.rstrip("\n")
            lines += 1
            if log:
                log.write(line + "\n")

            if (match := NINJA_STEP.match(line)):
                steps = int(match[1])
                advance(steps, int(match[2]), match[3])
                swallow_continuation = False
                preamble.clear()
                continue
            if (match := MAKE_STEP.match(line)):
                steps = int(match[1])
                advance(steps, 100, match[2])
                swallow_continuation = False
                preamble.clear()
                continue
            if (match := CTEST_STEP.match(line)):
                steps = int(match[1])
                advance(steps, int(match[2]), match[3])
                swallow_continuation = False
                preamble.clear()
                if match[4] and not match[4].startswith(CTEST_PASSED):
                    diag.errors += 1
                    live.console.print(
                        Text.assemble((f"{ERR} ", "err"), (match[3], "err"),
                                      ("  ", ""), (match[4], "muted"))
                    )
                continue
            if CTEST_FAILED_LINE.match(line):
                live.console.print(Text(line, style="err"))
                continue

            if (match := HEADLINE.match(line)):
                rendered = diag.record(match)
                is_error = "error" in match["kind"]
                if is_error:  # the instantiation chain is half the diagnosis
                    for held in preamble:
                        live.console.print(
                            Text(diag.shorten(held), style="faint"),
                            no_wrap=True, overflow="ellipsis", crop=True,
                        )
                preamble.clear()
                # An error keeps its source echo; a collapsed warning does not.
                swallow_continuation = rendered is None or not is_error
                if rendered is not None:
                    live.console.print(
                        rendered, no_wrap=not is_error,
                        overflow="ellipsis", crop=True,
                    )
                continue
            if NINJA_FAILED.match(line):
                swallow_continuation = False
                preamble.clear()
                live.console.print(Text(line, style="err"))
                continue
            if PREAMBLE.match(line):
                preamble.append(line)
                continue
            if swallow_continuation and CONTINUATION.match(line):
                continue
            if not line.strip():
                continue
            swallow_continuation = False
            preamble.clear()
            live.console.print(paint(line), no_wrap=True, overflow="ellipsis", crop=True)

    status = proc.wait()
    elapsed = time.monotonic() - started
    if log:
        log.close()

    # --- the closing report --------------------------------------------------
    if (table := diag.digest()) is not None:
        con.print()
        con.rule(Text("warnings", style="phase"), style="rule", align="left")
        con.print(table)
        if log_path:
            con.print(Text(f"  full text: {diag.relative(str(log_path))}",
                           style="faint", no_wrap=True, overflow="ellipsis"))
        con.print()

    detail = [f"{steps} steps" if steps else None, human(elapsed)]
    if diag.warnings:
        ours = sum(n for (_, mine), n in diag.tally.items() if mine)
        detail.append(f"{diag.warnings} warnings ({ours} ours)")
    if (cache := ccache_delta(before, ccache_stats())):
        detail.append(cache)
    tail = f" {NOTE} " + f" {NOTE} ".join(d for d in detail if d)

    if status == 0:
        if steps == 0 and lines <= 2:
            con.print(Text.assemble((f"{OK} ", "ok"), (label, "ok"),
                                    (f" {NOTE} already up to date", "faint")))
        else:
            con.print(Text.assemble((f"{OK} ", "ok"), (label, "ok"), (tail, "faint")))
    else:
        cmd_err([f"{label} failed after {human(elapsed)} (exit {status})"])
        if diag.errors:
            cmd_note([f"{diag.errors} error(s) above"])
        if log_path:
            cmd_note([f"full output: {diag.relative(str(log_path))}"])
    return status


_RULES = (
    (re.compile(r"\b(FAILED|fatal error|undefined reference|Error \d)"), "err"),
    (re.compile(r"^ninja:|^make.*Error|^cc1plus:"), "value"),
)


def paint(line: str) -> "Text":
    if "\033" in line:  # a toolchain that colours its own output is left alone
        return Text.from_ansi(line)
    for pattern, style in _RULES:
        if pattern.search(line):
            return Text(line, style=style)
    return Text(line, style="muted")


# --- make help ---------------------------------------------------------------
def cmd_helpfmt(_argv) -> int:
    con = console()
    # soft_wrap: let the terminal decide where a long option line breaks rather
    # than reflowing it at rich's 80-column default, which mangles `make help`
    # the moment it is piped.
    original = con.print
    con.print = lambda *a, **kw: original(*a, soft_wrap=True, **kw)  # type: ignore[method-assign]
    for index, raw in enumerate(sys.stdin.read().splitlines()):
        line = raw.rstrip()
        if index == 0:  # the title line becomes the wordmark
            print_wordmark(con)
            continue
        if not line:
            con.print()
        elif not line.startswith(" ") and line.endswith(":"):
            con.print(Text(line, style="phase bold"))
        elif (match := re.match(r"^(  make [\w.-]+(?: [A-Z]+=)?)(.*)$", line)):
            con.print(Text.assemble((match[1], "target"), (match[2], "muted")))
        elif (match := re.match(r"^(  [A-Z_]+=\S*)(.*)$", line)):
            con.print(Text.assemble((match[1], "value"), (match[2], "muted")))
        else:
            con.print(Text(line, style="muted"))
    return 0


COMMANDS = {
    "banner": cmd_banner,
    "step": cmd_step,
    "ok": cmd_ok,
    "warn": cmd_warn,
    "err": cmd_err,
    "note": cmd_note,
    "rule": cmd_rule,
    "run": cmd_run,
    "helpfmt": cmd_helpfmt,
}


def main(argv: list[str]) -> int:
    if not HAVE_RICH:
        print("build_ui.py: rich is not installed (run `make ui-deps`)", file=sys.stderr)
        return 3
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    handler = COMMANDS.get(argv[0])
    if handler is None:
        print(f"build_ui.py: unknown subcommand '{argv[0]}'", file=sys.stderr)
        return 2
    try:
        return handler(argv[1:]) or 0
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
