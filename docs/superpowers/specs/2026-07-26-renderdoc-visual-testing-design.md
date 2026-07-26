# RenderDoc Visual Testing and MCP Design

## Goal

Give Codex a repeatable way to build, launch, visually exercise, capture, and
inspect the game engine. The workflow must cover deterministic screenshots,
frame-time benchmarks, RenderDoc frame captures, and GPU-state inspection
without making RenderDoc a required runtime dependency.

The local machine will have a globally registered RenderDoc MCP server. The
repository will own all game-specific launch logic, artifact conventions, and
validation so the same commands remain useful to developers and CI without
Codex or the MCP server.

## Architecture

The integration has three layers:

1. The existing engine capture hooks provide fixed-step screenshots and
   benchmarks. A small optional frame-capture component dynamically discovers
   RenderDoc's in-application API and can begin and end exactly one requested
   frame.
2. `tools/visual_test.py` is the stable automation interface. It builds command
   lines, selects the graphical environment, launches the game, validates
   outputs, and emits JSON.
3. The globally registered `renderdoc-mcp` stdio server opens and queries the
   resulting `.rdc` files. It is an inspection consumer, not the owner of game
   launching or artifact policy.

This separation keeps the engine independent of MCP, keeps the MCP server
independent of repository conventions, and gives shell users the same
repeatable workflow available to an AI agent.

## Commands and Artifacts

`tools/visual_test.py` exposes the following subcommands:

- `probe`: report display, Xvfb, OpenGL, software-rendering, RenderDoc CLI,
  RenderDoc API, executable, and MCP readiness.
- `screenshot`: run a fixed-step scenario and produce a PNG.
- `benchmark`: run a fixed number of frames and record timing statistics.
- `capture`: produce one deterministic `.rdc` capture.
- `latest-artifact`: resolve the newest artifact of a requested kind.
- `validate`: check artifact presence, type, nonzero size, metadata, and
  command-specific invariants.

Every command prints one JSON result to stdout and returns nonzero on failure.
Diagnostics intended for humans go to stderr. Results share a versioned schema
with `status`, `operation`, `started_at`, `duration_seconds`, `command`,
`environment`, `artifacts`, `metrics`, `capabilities`, and `errors` fields.
Paths in JSON are absolute.

The default output root is `artifacts/visual/`, overridable with
`ARTIFACT_DIR`. Each invocation creates a timestamped run directory and
atomically updates a small `latest.json` manifest after successful validation.
Partial or failed runs remain available for diagnosis but never replace the
latest successful manifest. Generated artifacts are ignored by git.

The Makefile provides thin entry points:

- `make visual-test`
- `make visual-bench`
- `make renderdoc-capture`

Existing variables such as `SEED`, `PRESET`, `MAP`, `FRAME`, and `FIXED_DT`
continue to define the scenario. The wrappers do not duplicate policy already
implemented by the Python tool.

## Display and Rendering Strategy

An existing usable X11 display and hardware OpenGL are preferred. When no
display is available, the runner creates an isolated Xvfb session with its own
temporary Xauthority data. Software rendering through llvmpipe is an explicit
fallback for screenshots and benchmarks.

RenderDoc capture requires an injectable GL process. If the selected
headless/software configuration cannot be captured, `probe` and `capture`
return a precise unsupported-capability error rather than silently substituting
a screenshot. Temporary display resources are always cleaned up.

The runner forces windowed mode, disables vsync for benchmarks and captures,
uses the build directory as the game working directory, pins the fixed
timestep and seed, waits for process exit, and records the effective
environment in metadata.

## RenderDoc Capture

The engine's frame-capture component loads RenderDoc's application API at
runtime from the already injected module. There is no link-time dependency.
When `PSX_RENDERDOC_FRAME=<n>` is present, it starts capture immediately before
rendering that frame and ends capture immediately afterward.
`PSX_RENDERDOC_CAPTURE=<template>` controls the output path.

If the API is absent, ordinary game runs are unaffected. If capture was
requested but unavailable, the engine logs a clear failure and the automation
layer treats a missing `.rdc` as an error. Frame numbers must be positive and
capture is limited to one frame per process.

The external launcher wraps `renderdoccmd capture` with the game executable,
working directory, deterministic environment, no fullscreen, no vsync, output
template, and wait-for-exit behavior. The in-application trigger selects the
exact frame while `renderdoccmd` supplies injection and capture-file handling.

## Global MCP Integration

The supported server is the local `renderdoc-mcp` stdio implementation backed
by RenderDoc 1.45. It will be installed outside the repository in an isolated
Python environment and registered in the user's global Codex MCP
configuration. The command, module path, RenderDoc module path, and required
environment variables must use absolute paths.

The repository includes a configuration example and setup/troubleshooting
documentation, but does not vendor the server, commit user-specific paths, or
silently download third-party code. Setup is idempotent: an existing
registration is inspected and updated only when its effective configuration is
wrong.

Successful MCP verification means Codex can connect to the server, open the
newest capture, enumerate frame actions, inspect pipeline state and resources,
and retrieve a representative texture preview or pixel value.

## Failure Handling

Capability failures are distinguished from game failures and artifact
validation failures. Error JSON includes a stable code, a concise message, and
an actionable remedy. Expected cases include missing Xvfb, unusable display,
missing GL support, missing RenderDoc CLI/API, injection failure, game crash,
timeout, and empty or malformed artifacts.

Subprocesses have bounded timeouts and process-group cleanup. The runner never
deletes artifacts outside its resolved run directory. Existing successful
artifacts are not overwritten.

## Testing and Acceptance

Unit tests cover command construction, environment selection, capability JSON,
schema stability, latest-artifact selection, validation, and missing dependency
diagnostics. Frame-capture tests cover environment parsing, one-frame state
transitions, unavailable API behavior, and capture-path selection without
requiring RenderDoc to be loaded.

End-to-end acceptance on this machine requires:

1. A successful capability probe.
2. A nonempty deterministic PNG and valid JSON result.
3. A benchmark result with frame count and timing statistics.
4. A nonempty `.rdc` captured from the game at the requested frame.
5. A globally registered MCP server that opens that capture and returns
   meaningful event, pipeline, resource, and texture inspection results.
6. Focused tests, the game build, and `git diff --check` passing.

## Non-goals

- Pixel-perfect screenshot equality for nondeterministic particle output.
- Automated gameplay bots or semantic computer-vision judgments.
- Vulkan support; the engine currently uses OGRE GL3Plus.
- Making RenderDoc, Xvfb, Codex, or the MCP server mandatory to build or run
  the game normally.
- Uploading captures or screenshots to external services.
