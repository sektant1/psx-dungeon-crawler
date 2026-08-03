# RenderDoc and AI Visual Testing

This project can produce deterministic screenshots, frame-time reports, and
single-frame RenderDoc captures. Codex can inspect the resulting `.rdc` through
the globally registered `renderdoc` MCP server.

## Prerequisites

- A built `game` target (`make build-game`).
- RenderDoc 1.45 or compatible, including `renderdoccmd`.
- An X11 display, or Xvfb for headless execution.
- Mesa software OpenGL when hardware GL is unavailable.
- For AI inspection, the external `renderdoc-mcp` Python package registered in
  the user's global Codex MCP configuration.

None of these tools are required for an ordinary engine build except the normal
graphics stack. The engine discovers RenderDoc only after its module has been
injected into the process.

## Quick Start

```bash
python tools/visual_test.py probe
make visual-test FRAME=90 SEED=1 PRESET=ps1
make visual-bench BENCH=120
make renderdoc-capture FRAME=90 SEED=1
```

Output defaults to `artifacts/visual/`. Override it with
`ARTIFACT_DIR=/absolute/path`. Each successful run updates
`artifacts/visual/latest.json`; failed run directories remain available for
diagnosis and do not replace the successful manifest.

Existing Make variables are supported: `BUILD_DIR`, `ARTIFACT_DIR`, `FRAME`,
`FIXED_DT`, `SEED`, `PRESET`, `MAP`, `BENCH`, and `DISPLAY_MODE`.

## JSON Interface

The underlying CLI is the stable AI-facing interface:

```bash
python tools/visual_test.py probe
python tools/visual_test.py screenshot --frame 90
python tools/visual_test.py benchmark --frames 120
python tools/visual_test.py capture --frame 90
python tools/visual_test.py latest-artifact capture
python tools/visual_test.py validate /absolute/path/frame.rdc --kind capture
```

Global options such as `--build-dir` and `--artifact-dir` precede the
subcommand. Every invocation prints exactly one `raven-visual-test/v1` JSON
object to stdout. Child logs are captured into the run directory. The result
contains status, operation, command, effective capture environment, artifacts,
metrics, capabilities, and structured errors. A failed operation exits nonzero.

## Display Selection

The runner uses the current X11 display when it responds to a display probe.
With no usable display it invokes `xvfb-run` with an isolated temporary display
and Xauthority file and enables llvmpipe through `LIBGL_ALWAYS_SOFTWARE=1`.
Screenshot and benchmark operations support this fallback.

Use `DISPLAY_MODE=current` to require the persistent host display (recommended
for captures that will be replayed through MCP), or `DISPLAY_MODE=xvfb` to force
the isolated software-rendering path. `auto` is the default.

RenderDoc needs an injectable OpenGL process. A capture failure under Xvfb or
software GL is reported as `process_failed` or `missing_artifact`; it is never
silently replaced with a screenshot. Inspect the accompanying `process.log`.

## Exact-Frame Capture

`renderdoccmd capture` performs process injection. The runner also sets:

```text
RAVEN_RENDERDOC_FRAME=<positive frame number>
RAVEN_RENDERDOC_CAPTURE=<absolute capture template>
```

The engine dynamically obtains RenderDoc's 1.6 application API and brackets
exactly that rendered frame with `StartFrameCapture`/`EndFrameCapture`. No
RenderDoc symbols are linked into the game. Invalid frame values disable the
trigger; a requested capture with no injected API emits a clear engine error.

## Global Codex MCP

Inspect the current global registration:

```bash
codex mcp get renderdoc
```

The entry should run the isolated Python environment outside this repository:

```text
/absolute/path/renderdoc-mcp/.venv/bin/python -m renderdoc_mcp
```

It also needs absolute `PYTHONPATH` and `RENDERDOC_MODULE_PATH` environment
values. See `tools/renderdoc-mcp.example.toml`. Use `codex mcp add` to register
it globally. Do not commit the user's actual global paths or silently download
the server from repository scripts.

After `make renderdoc-capture`, ask Codex to:

1. Resolve the latest capture with `latest-artifact capture`.
2. Open that absolute `.rdc` through the RenderDoc MCP.
3. Enumerate actions/events and inspect the GL pipeline at a draw call.
4. List textures/buffers and inspect a representative color target.
5. Report API validation messages and suspicious state or resource bindings.

Server startup alone is not proof of correct integration; a real capture must
be opened and queried.

## Troubleshooting

- `missing_game`: run `make build-game` or pass `--build-dir`.
- `missing_display`: install Xvfb or provide a usable X11 `DISPLAY`.
- `missing_renderdoc`: install RenderDoc and confirm `renderdoccmd --version`.
- `process_failed`: open the run's `process.log`; common causes are GLX
  injection problems, bad assets, or an unusable display.
- `missing_metrics`: the game exited before its warm-up plus requested
  benchmark frames completed.
- `missing_artifact`: RenderDoc injected but did not complete the requested
  frame. Confirm `RAVEN_RENDERDOC_FRAME` is reached and capture is not blocked by
  another RenderDoc instance.
- MCP startup/import errors: verify the configured interpreter, `PYTHONPATH`,
  and `RENDERDOC_MODULE_PATH` all exist and belong to compatible Python and
  RenderDoc versions.
