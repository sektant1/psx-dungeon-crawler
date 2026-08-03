#!/usr/bin/env python3
"""Deterministic visual, benchmark, and RenderDoc automation for the game."""

from __future__ import annotations

import argparse
import dataclasses
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

SCHEMA = "raven-visual-test/v1"
PROJECT_ROOT = Path(__file__).resolve().parents[1]
KIND_SUFFIX = {"screenshot": ".png", "benchmark": ".json", "capture": ".rdc"}


@dataclasses.dataclass
class VisualError:
    code: str
    message: str
    remedy: str = ""

    def as_dict(self) -> dict[str, str]:
        return dataclasses.asdict(self)


@dataclasses.dataclass
class VisualResult:
    operation: str
    status: str = "ok"
    started_at: str = dataclasses.field(
        default_factory=lambda: dt.datetime.now(dt.timezone.utc).isoformat()
    )
    duration_seconds: float = 0.0
    command: list[str] = dataclasses.field(default_factory=list)
    environment: dict[str, str] = dataclasses.field(default_factory=dict)
    artifacts: list[dict[str, Any]] = dataclasses.field(default_factory=list)
    metrics: dict[str, Any] = dataclasses.field(default_factory=dict)
    capabilities: dict[str, Any] = dataclasses.field(default_factory=dict)
    errors: list[VisualError] = dataclasses.field(default_factory=list)

    def fail(self, error: VisualError) -> None:
        self.status = "error"
        self.errors.append(error)

    def as_dict(self) -> dict[str, Any]:
        return {
            "schema": SCHEMA,
            "status": self.status,
            "operation": self.operation,
            "started_at": self.started_at,
            "duration_seconds": round(self.duration_seconds, 6),
            "command": self.command,
            "environment": self.environment,
            "artifacts": self.artifacts,
            "metrics": self.metrics,
            "capabilities": self.capabilities,
            "errors": [error.as_dict() for error in self.errors],
        }


def deterministic_environment(
    base: dict[str, str],
    *,
    frame: int,
    fixed_dt: str,
    seed: int,
    preset: str | None,
) -> dict[str, str]:
    env = dict(base)
    env.pop("RAVEN_FULLSCREEN", None)
    env.update(
        {
            "SDL_VIDEODRIVER": "x11",
            "RAVEN_FIXED_DT": fixed_dt,
            "RAVEN_GEN_SEED": str(seed),
            "RAVEN_SCREENSHOT_FRAME": str(max(1, frame)),
        }
    )
    if preset:
        env["RAVEN_RENDER_PRESET"] = preset
    return env


_FRAME_STATS = re.compile(
    r"FrameStats:\s+n=(\d+)\s+p50=([\d.]+)ms\s+p95=([\d.]+)ms\s+"
    r"p99=([\d.]+)ms\s+max=([\d.]+)ms"
)


def parse_frame_stats(text: str) -> dict[str, int | float] | None:
    match = _FRAME_STATS.search(text)
    if not match:
        return None
    return {
        "n": int(match.group(1)),
        "p50_ms": float(match.group(2)),
        "p95_ms": float(match.group(3)),
        "p99_ms": float(match.group(4)),
        "max_ms": float(match.group(5)),
    }


def _validate_file(path: Path, signature: bytes, label: str) -> VisualError | None:
    if not path.exists():
        return VisualError("missing_artifact", f"{label} does not exist: {path}")
    if path.stat().st_size == 0:
        return VisualError("empty_artifact", f"{label} is empty: {path}")
    if path.read_bytes()[: len(signature)] != signature:
        return VisualError(
            "invalid_artifact", f"{label} has an invalid signature: {path}"
        )
    return None


def validate_png(path: Path) -> VisualError | None:
    return _validate_file(path, b"\x89PNG\r\n\x1a\n", "PNG")


def validate_rdc(path: Path) -> VisualError | None:
    return _validate_file(path, b"RDOC", "RenderDoc capture")


def atomic_write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=path.parent, delete=False
    ) as stream:
        json.dump(payload, stream, indent=2, sort_keys=True)
        stream.write("\n")
        temporary = Path(stream.name)
    temporary.replace(path)


def latest_artifact(root: Path, kind: str) -> Path | None:
    manifest = root / "latest.json"
    if manifest.is_file():
        try:
            payload = json.loads(manifest.read_text(encoding="utf-8"))
            for artifact in payload.get("artifacts", []):
                if artifact.get("kind") == kind:
                    path = Path(artifact["path"])
                    if path.exists():
                        return path.resolve()
        except (OSError, ValueError, KeyError, TypeError):
            pass
    suffix = KIND_SUFFIX.get(kind)
    if not suffix:
        return None
    matches = [path for path in root.rglob(f"*{suffix}") if path.is_file()]
    return max(matches, key=lambda path: path.stat().st_mtime_ns).resolve() if matches else None


def _capability(command: str) -> dict[str, Any]:
    path = shutil.which(command)
    return {"available": bool(path), "path": str(Path(path).resolve()) if path else None}


def _display_usable(env: dict[str, str]) -> bool:
    if not env.get("DISPLAY"):
        return False
    probe = shutil.which("xdpyinfo") or shutil.which("glxinfo")
    if not probe:
        return False
    try:
        return (
            subprocess.run(
                [probe],
                env=env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=5,
                check=False,
            ).returncode
            == 0
        )
    except (OSError, subprocess.TimeoutExpired):
        return False


def probe_capabilities(build_dir: Path) -> dict[str, Any]:
    env = dict(os.environ)
    display = _display_usable(env)
    mcp = subprocess.run(
        ["codex", "mcp", "get", "renderdoc"],
        text=True,
        capture_output=True,
        timeout=10,
        check=False,
    ) if shutil.which("codex") else None
    return {
        "game": {
            "available": (build_dir / "game").is_file(),
            "path": str((build_dir / "game").resolve()),
            "scene_editor": (build_dir / "scene_editor").is_file(),
        },
        "display": {"available": display, "name": env.get("DISPLAY")},
        "xvfb": _capability("Xvfb"),
        "xvfb_run": _capability("xvfb-run"),
        "glxinfo": _capability("glxinfo"),
        "renderdoccmd": _capability("renderdoccmd"),
        "codex": _capability("codex"),
        "renderdoc_mcp": {
            "available": bool(mcp and mcp.returncode == 0),
            "detail": (mcp.stdout.strip() if mcp and mcp.returncode == 0 else ""),
        },
        "renderdoc_app_api": {
            "available": bool(shutil.which("renderdoccmd")),
            "detail": "discovered in-process after RenderDoc injection",
        },
    }


def _run_dir(root: Path, operation: str) -> Path:
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    path = (root / f"{stamp}-{operation}").resolve()
    path.mkdir(parents=True, exist_ok=False)
    return path


def _display_prefix(env: dict[str, str], mode: str = "auto") -> list[str]:
    if mode == "current":
        if not env.get("DISPLAY"):
            raise RuntimeError("Current display requested but DISPLAY is unset")
        return []
    if mode == "auto" and _display_usable(env):
        return []
    xvfb_run = shutil.which("xvfb-run")
    if not xvfb_run:
        raise RuntimeError("No usable X11 display and xvfb-run is not installed")
    env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
    return [
        xvfb_run,
        "-a",
        "-s",
        "-screen 0 1280x720x24 +extension GLX +render -noreset",
    ]


def _run_process(
    command: list[str], *, cwd: Path, env: dict[str, str], timeout: int
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        env=env,
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
        start_new_session=True,
    )


def _artifact(kind: str, path: Path) -> dict[str, Any]:
    return {"kind": kind, "path": str(path.resolve()), "bytes": path.stat().st_size}


def _common_env(args: argparse.Namespace) -> dict[str, str]:
    return deterministic_environment(
        os.environ,
        frame=args.frame,
        fixed_dt=args.fixed_dt,
        seed=args.seed,
        preset=args.preset,
    )


def run_visual(args: argparse.Namespace, result: VisualResult) -> None:
    build_dir = Path(args.build_dir).resolve()
    # Any engine consumer, not just the game: the editor and the demo run the
    # same renderer, so a shader regression or a GPU capture is just as valid
    # from either -- and the editor is often the easier one to reproduce in.
    app_name = getattr(args, "app", None) or "game"
    game = build_dir / app_name
    if not game.is_file():
        result.fail(
            VisualError(
                "missing_app",
                f"Executable not found: {game}",
                f"Run make build-{'editor' if app_name == 'scene_editor' else app_name}",
            )
        )
        return

    root = Path(args.artifact_dir).resolve()
    run_dir = _run_dir(root, args.operation)
    env = _common_env(args)
    env["ALSOFT_DRIVERS"] = "null"
    game_args = [str(Path(args.map).resolve())] if args.map else []
    if getattr(args, "scene", None):
        game_args = [str(Path(args.scene).resolve())]
    try:
        prefix = _display_prefix(env, args.display_mode)
    except RuntimeError as exc:
        result.fail(VisualError("missing_display", str(exc), "Install Xvfb or provide DISPLAY"))
        return

    if args.operation == "screenshot":
        output = run_dir / "screenshot.png"
        env["RAVEN_SCREENSHOT"] = str(output)
        command = prefix + [str(game), *game_args]
    elif args.operation == "benchmark":
        env.pop("RAVEN_FIXED_DT", None)
        env["RAVEN_BENCH_FRAMES"] = str(args.frames)
        command = prefix + [str(game), *game_args]
        output = run_dir / "benchmark.json"
    else:
        renderdoccmd = shutil.which("renderdoccmd")
        if not renderdoccmd:
            result.fail(VisualError("missing_renderdoc", "renderdoccmd is not installed"))
            return
        template = run_dir / "capture"
        env["RAVEN_RENDERDOC_FRAME"] = str(args.frame)
        env["RAVEN_RENDERDOC_CAPTURE"] = str(template)
        command = prefix + [
            renderdoccmd,
            "capture",
            "--wait-for-exit",
            "--opt-disallow-vsync",
            "--opt-disallow-fullscreen",
            "--working-dir",
            str(build_dir),
            "--capture-file",
            str(template),
            str(game),
            *game_args,
        ]
        output = template

    result.command = command
    result.environment = {
        key: env[key]
        for key in sorted(env)
        if key.startswith("RAVEN_")
        or key in {"DISPLAY", "SDL_VIDEODRIVER", "LIBGL_ALWAYS_SOFTWARE"}
    }
    try:
        completed = _run_process(command, cwd=build_dir, env=env, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        result.fail(VisualError("timeout", f"Game exceeded {args.timeout}s timeout"))
        return

    log_path = run_dir / "process.log"
    log_path.write_text(completed.stdout + completed.stderr, encoding="utf-8")
    result.artifacts.append(_artifact("log", log_path))
    if completed.returncode != 0:
        result.fail(
            VisualError("process_failed", f"Process exited with {completed.returncode}", str(log_path))
        )
        return

    if args.operation == "screenshot":
        error = validate_png(output)
        if error:
            result.fail(error)
            return
        result.artifacts.append(_artifact("screenshot", output))
    elif args.operation == "benchmark":
        metrics = parse_frame_stats(completed.stdout + completed.stderr)
        if not metrics:
            result.fail(VisualError("missing_metrics", "FrameStats line was not emitted", str(log_path)))
            return
        result.metrics = metrics
        atomic_write_json(output, metrics)
        result.artifacts.append(_artifact("benchmark", output))
    else:
        captures = sorted(run_dir.glob("capture*.rdc"), key=lambda path: path.stat().st_mtime_ns)
        if not captures:
            result.fail(VisualError("missing_artifact", "RenderDoc produced no capture", str(log_path)))
            return
        capture = captures[-1]
        error = validate_rdc(capture)
        if error:
            result.fail(error)
            return
        result.artifacts.append(_artifact("capture", capture))

    atomic_write_json(root / "latest.json", result.as_dict())


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default=os.environ.get("BUILD_DIR", "build"))
    parser.add_argument(
        "--artifact-dir", default=os.environ.get("ARTIFACT_DIR", "artifacts/visual")
    )
    subparsers = parser.add_subparsers(dest="operation", required=True)
    subparsers.add_parser("probe", help="report visual and RenderDoc capabilities")
    for name in ("screenshot", "benchmark", "capture"):
        child = subparsers.add_parser(name, help=f"run a {name} operation")
        child.add_argument("--frame", type=int, default=90)
        child.add_argument("--fixed-dt", default="0.016666667")
        child.add_argument("--seed", type=int, default=1)
        child.add_argument("--preset")
        child.add_argument("--map")
        child.add_argument(
            "--app",
            default="game",
            help="which built executable to drive (game, scene_editor, psx_demo)",
        )
        child.add_argument("--scene", help=".scn to open (scene_editor only)")
        child.add_argument("--frames", type=int, default=120)
        child.add_argument("--timeout", type=int, default=180)
        child.add_argument(
            "--display-mode",
            choices=("auto", "current", "xvfb"),
            default=os.environ.get("VISUAL_DISPLAY_MODE", "auto"),
        )
    latest = subparsers.add_parser("latest-artifact", help="find newest artifact")
    latest.add_argument("kind", choices=sorted(KIND_SUFFIX))
    validate = subparsers.add_parser("validate", help="validate an artifact")
    validate.add_argument("path")
    validate.add_argument("--kind", choices=("screenshot", "capture"), required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    result = VisualResult(operation=args.operation)
    started = time.monotonic()
    try:
        if args.operation == "probe":
            result.capabilities = probe_capabilities(Path(args.build_dir).resolve())
        elif args.operation == "latest-artifact":
            path = latest_artifact(Path(args.artifact_dir).resolve(), args.kind)
            if path:
                result.artifacts.append(_artifact(args.kind, path))
            else:
                result.fail(VisualError("missing_artifact", f"No {args.kind} artifact found"))
        elif args.operation == "validate":
            path = Path(args.path).resolve()
            error = validate_png(path) if args.kind == "screenshot" else validate_rdc(path)
            if error:
                result.fail(error)
            elif path.exists():
                result.artifacts.append(_artifact(args.kind, path))
        else:
            run_visual(args, result)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        result.fail(VisualError("operation_failed", str(exc)))
    result.duration_seconds = time.monotonic() - started
    print(json.dumps(result.as_dict(), sort_keys=True))
    return 0 if result.status == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
