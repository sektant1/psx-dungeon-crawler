#!/usr/bin/env python3
"""The build's presentation layer -- the rich-powered half.

`tools/build-ui.sh` is what the Makefile recipes call. It hands over to this
script when a python3 with `rich` is importable and renders the same information
itself, in bash, when it is not. Keeping bash as the floor is deliberate:
building the game must not depend on a python package being installed, and the
one machine where the pretty renderer is missing is usually the one where
something is already wrong.

    make ui-deps        put rich in .cache/py (repo-local, nothing system-wide)

The subcommands mirror build-ui.sh exactly, so either half can serve any recipe:

    build_ui.py banner [--full] [detail...]
    build_ui.py step|ok|warn|err|note MSG...
    build_ui.py rule [TITLE]
    build_ui.py run LABEL -- CMD...
    build_ui.py helpfmt < text

`run` is the interesting one: it drives CMD, turns ninja's `[n/m]` chatter into a
progress bar, keeps compiler diagnostics readable above it, tees an uncoloured
transcript to $RAVEN_BUILD_LOG_DIR/last-build.log, reports what ccache did with
the build, and exits with CMD's own status.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

try:
    from rich.console import Console, Group
    from rich.live import Live
    from rich.progress import (
        BarColumn,
        MofNCompleteColumn,
        Progress,
        SpinnerColumn,
        TaskProgressColumn,
        TextColumn,
        TimeElapsedColumn,
        TimeRemainingColumn,
    )
    from rich.text import Text
    from rich.theme import Theme

    HAVE_RICH = True
except ImportError:  # the bash half normally catches this before we are called
    HAVE_RICH = False


# --- theme -------------------------------------------------------------------
# Named after meaning rather than colour, so the whole build can be retuned from
# one dict. Violet is the engine, cyan names things, amber is a value.
THEME = {
    "engine": "bold #b48ead",
    "phase": "#c3b0d9",
    "target": "bold #7ececd",
    "value": "#e0a458",
    "ok": "#8fbf6f",
    "warn": "#d8b34a",
    "err": "bold #e06c75",
    "muted": "grey50",
    "bar.complete": "#b48ead",
    "bar.finished": "#8fbf6f",
    "bar.pulse": "#7ececd",
}

MARK, STEP, OK, WARN, ERR, NOTE = "✦", "▸", "✔", "▲", "✖", "·"


def console(stderr: bool = False) -> "Console":
    return Console(
        theme=Theme(THEME),
        stderr=stderr,
        # A terminal that reports no colour, NO_COLOR, or PLAIN=1 all land here.
        no_color=bool(os.environ.get("NO_COLOR"))
        or os.environ.get("RAVEN_BUILD_PLAIN") not in (None, "", "0", "false"),
        highlight=False,
        soft_wrap=False,
    )


# --- log lines ---------------------------------------------------------------
def _say(glyph: str, style: str, msg: str, stderr: bool = False) -> None:
    console(stderr).print(Text.assemble((f"{glyph} ", style), (msg, style)))


def cmd_step(a): _say(STEP, "phase", " ".join(a))
def cmd_ok(a): _say(OK, "ok", " ".join(a))
def cmd_warn(a): _say(WARN, "warn", " ".join(a))
def cmd_err(a): _say(ERR, "err", " ".join(a), stderr=True)
def cmd_note(a): _say(NOTE, "muted", " ".join(a))


def cmd_rule(a) -> None:
    con = console()
    title = " ".join(a)
    con.rule(Text(title, style="phase") if title else "", style="grey30", align="left")


WORDMARK = ("  █▀▄ ▄▀█ █ █ █▀▀ █▄ █", "  █▀▄ █▀█ ▀▄▀ ██▄ █ ▀█")


def cmd_banner(a) -> None:
    con = console()
    full = bool(a) and a[0] == "--full"
    detail = " ".join(a[1:] if full else a)
    if full:
        con.print()
        con.print(Text(WORDMARK[0], style="engine"))
        con.print(
            Text.assemble(
                (WORDMARK[1], "engine"),
                ("   a psx dungeon crawler engine", "muted"),
            )
        )
        con.print()
        if detail:
            cmd_note([detail])
        return
    con.print(
        Text.assemble(
            (f"{MARK} raven", "engine"), (f" {NOTE} ", "muted"), (detail, "muted")
        )
    )


# --- compiler diagnostics ----------------------------------------------------
_RULES = (
    (re.compile(r"\b(FAILED|fatal error|undefined reference|error):?"), "err"),
    (re.compile(r"\bwarning:"), "warn"),
    (re.compile(r"\b(note:|required from|In file included from|In instantiation)"), "muted"),
    (re.compile(r"^ninja:|^make.*Error"), "value"),
)


def paint(line: str) -> "Text":
    if "\033" in line:  # a toolchain that colours its own output is left alone
        return Text.from_ansi(line)
    for pattern, style in _RULES:
        if pattern.search(line):
            return Text(line, style=style)
    return Text(line)


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
    """What ccache did during the build, as one clause -- or None if it did
    nothing worth reporting (a link-only or up-to-date build)."""
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
        parts.append(f"ccache {hits}/{total} hits ({hits * 100 // total}%)")
    if bypassed:
        parts.append(f"{bypassed} bypassed -- run `make doctor`")
    return ", ".join(parts)


# --- run ---------------------------------------------------------------------
NINJA_STEP = re.compile(r"^\[\s*(\d+)/(\d+)\]\s*(.*)$")   # ninja
MAKE_STEP = re.compile(r"^\[\s*(\d+)%\]\s*(.*)$")          # makefile generator


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
    log = None
    if log_dir:
        try:
            Path(log_dir).mkdir(parents=True, exist_ok=True)
            log = open(Path(log_dir) / "last-build.log", "w", errors="replace")
        except OSError:
            log = None

    env = dict(os.environ)
    # Pin the status format the bar parses: a user's NINJA_STATUS would
    # otherwise silently turn progress reporting off.
    env["NINJA_STATUS"] = "[%f/%t] "

    before = ccache_stats()
    started = time.monotonic()

    progress = Progress(
        SpinnerColumn(style="bar.pulse"),
        TextColumn("[phase]{task.description}[/]"),
        BarColumn(
            bar_width=None,
            complete_style="bar.complete",
            finished_style="bar.finished",
            pulse_style="bar.pulse",
        ),
        TaskProgressColumn(),
        MofNCompleteColumn(),
        TimeElapsedColumn(),
        TimeRemainingColumn(compact=True),
        console=con,
        transient=True,
        expand=True,
    )
    action = Text("", style="muted", no_wrap=True, overflow="ellipsis")
    task = progress.add_task(label, total=None)

    steps = 0
    lines = 0
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

    with Live(Group(progress, action), console=con, refresh_per_second=12) as live:
        assert proc.stdout is not None
        for raw in proc.stdout:
            line = raw.rstrip("\n")
            lines += 1
            if log:
                log.write(line + "\n")

            match = NINJA_STEP.match(line)
            if match:
                done, total, what = int(match[1]), int(match[2]), match[3]
                steps = done
                progress.update(task, completed=done, total=total)
                action.plain = "  " + what
                continue
            match = MAKE_STEP.match(line)
            if match:
                steps = int(match[1])
                progress.update(task, completed=steps, total=100)
                action.plain = "  " + match[2]
                continue
            live.console.print(paint(line))

    status = proc.wait()
    elapsed = time.monotonic() - started
    if log:
        log.close()

    detail = [f"{steps} steps" if steps else None, human(elapsed)]
    cache = ccache_delta(before, ccache_stats())
    if cache:
        detail.append(cache)
    tail = f" {NOTE} " + f" {NOTE} ".join(d for d in detail if d)

    if status == 0:
        if steps == 0 and lines <= 2:
            con.print(Text.assemble((f"{OK} ", "ok"), (label, "ok"),
                                    (f" {NOTE} already up to date", "muted")))
        else:
            con.print(Text.assemble((f"{OK} ", "ok"), (label, "ok"), (tail, "muted")))
    else:
        cmd_err([f"{label} failed after {human(elapsed)} (exit {status})"])
        if log_dir:
            cmd_note([f"full output: {Path(log_dir) / 'last-build.log'}"])
    return status


# --- make help ---------------------------------------------------------------
def cmd_helpfmt(_argv) -> int:
    con = console()
    for index, raw in enumerate(sys.stdin.read().splitlines()):
        line = raw.rstrip()
        if index == 0:  # the title line becomes the wordmark
            con.print(Text(WORDMARK[0], style="engine"))
            con.print(
                Text.assemble((WORDMARK[1], "engine"),
                              ("   a psx dungeon crawler engine", "muted"))
            )
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
