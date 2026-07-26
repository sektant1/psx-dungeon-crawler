# RenderDoc Visual Testing and MCP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a deterministic screenshot, benchmark, and RenderDoc capture workflow and register a working RenderDoc inspection MCP globally for Codex.

**Architecture:** A dependency-free C++ frame-capture adapter dynamically calls the injected RenderDoc 1.45 API around one engine frame. A Python CLI owns display selection, deterministic process launch, JSON reporting, artifacts, and validation; Make targets provide short developer entry points. The external MCP server consumes completed `.rdc` files and remains independent of the engine.

**Tech Stack:** C++20, OGRE GL3Plus/OpenGL, SDL2/X11, RenderDoc 1.45 C API and CLI, Python 3 standard library, CMake/CTest, GNU Make, Xvfb/Mesa llvmpipe, Codex MCP.

## Global Constraints

- RenderDoc must remain an optional runtime dependency and must not affect ordinary game startup.
- The default artifact root is `artifacts/visual/`; all JSON artifact paths are absolute.
- The runner prints exactly one versioned JSON result to stdout and human diagnostics to stderr.
- Prefer an existing X11/hardware-GL session; use isolated Xvfb and llvmpipe only as fallbacks.
- Capture exactly one positive-numbered frame per process.
- Do not vendor or silently download the MCP server; install it outside the repository and register absolute global paths.
- Preserve unrelated working-tree changes.
- Vulkan, gameplay bots, external uploads, and pixel-perfect particle comparisons are out of scope.

---

### Task 1: Optional in-engine RenderDoc frame trigger

**Files:**
- Create: `engine/include/eng/render/FrameCapture.h`
- Create: `engine/src/render/FrameCapture.cpp`
- Create: `engine/tests/FrameCaptureTests.cpp`
- Modify: `engine/src/Engine.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: environment variables `PSX_RENDERDOC_FRAME` and `PSX_RENDERDOC_CAPTURE`.
- Produces: `eng::FrameCapture::fromEnvironment()`, `beforeFrame(int)`, `afterFrame(int)`, `requested()`, and `failed()`.

- [ ] **Step 1: Write failing state-machine tests**

Create tests using an injected fake API callback table. Verify invalid/absent
frame values disable capture, frame `7` starts only before frame 7, ends only
after frame 7, never starts twice, and API absence reports failure only when a
capture was requested.

- [ ] **Step 2: Run the focused test and verify failure**

Run: `cmake --build build --target frame_capture_tests -j2`

Expected: FAIL because the target and implementation do not exist.

- [ ] **Step 3: Implement the adapter**

Declare only the RenderDoc ABI subset required for
`RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0)`,
`SetCaptureFilePathTemplate`, `StartFrameCapture`, and `EndFrameCapture`.
Resolve `RENDERDOC_GetAPI` with `dlsym(RTLD_DEFAULT, ...)`. Keep parsing and the
state machine testable without a loaded RenderDoc module.

- [ ] **Step 4: Integrate around one rendered frame**

Construct capture state during `Engine::init()`. Call `beforeFrame(frameCount +
1)` immediately before `RenderCore::renderFrame()` and `afterFrame(frameCount +
1)` immediately after it. Log requested capture failures without terminating
ordinary runs.

- [ ] **Step 5: Build and test**

Run: `cmake --build build --target frame_capture_tests game -j2 && build/frame_capture_tests`

Expected: PASS and game links without RenderDoc.

- [ ] **Step 6: Commit**

Commit only the frame-capture files and their minimal CMake/Engine hunks with
message `feat: add optional RenderDoc frame capture trigger`.

---

### Task 2: JSON visual-test CLI

**Files:**
- Create: `tools/visual_test.py`
- Create: `tools/tests/test_visual_test.py`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: built `game`, `renderdoccmd`, display environment, existing
  `PSX_SCREENSHOT*`, `PSX_BENCH_FRAMES`, and Task 1 capture variables.
- Produces: subcommands `probe`, `screenshot`, `benchmark`, `capture`,
  `latest-artifact`, and `validate`; schema identifier
  `psx-visual-test/v1`.

- [ ] **Step 1: Write failing Python unit tests**

Use `unittest`, temporary directories, and mocked `subprocess.run`/`shutil.which`
to cover deterministic environment construction, display choice, command
construction, FrameStats parsing, atomic latest manifest updates, newest
artifact selection, PNG/RDC signatures and nonzero-size validation, stable
error codes, and stdout JSON shape.

- [ ] **Step 2: Verify the tests fail**

Run: `python -m unittest discover -s tools/tests -p 'test_visual_test.py' -v`

Expected: FAIL because `tools.visual_test` does not exist.

- [ ] **Step 3: Implement shared result and artifact primitives**

Add `VisualResult`, `VisualError`, `ArtifactRun`, `atomic_write_json()`,
`validate_png()`, `validate_rdc()`, `parse_frame_stats()`, and
`latest_artifact()`. Resolve every emitted path with `Path.resolve()`.

- [ ] **Step 4: Implement capability and display probing**

Report executable, display, Xvfb, `glxinfo`, GL, llvmpipe, RenderDoc CLI, and
global Codex MCP registration as structured capability entries. Select the
current X11 display only when it passes a short probe; otherwise create a
temporary Xvfb display and Xauthority file with process-group cleanup.

- [ ] **Step 5: Implement screenshot and benchmark launchers**

Launch from the build directory with `SDL_VIDEODRIVER=x11`,
`PSX_FIXED_DT=0.016666667`, a pinned seed, windowed mode, and bounded timeout.
For screenshots validate the PNG. For benchmarks parse the logged `FrameStats`
line into `n`, `p50_ms`, `p95_ms`, `p99_ms`, and `max_ms`.

- [ ] **Step 6: Implement RenderDoc capture**

Wrap `renderdoccmd capture --wait-for-exit` using a deterministic output
template and Task 1's exact-frame variables. Resolve the produced capture even
when RenderDoc appends a frame suffix, validate its RDC signature/size, and
include the capture log in artifacts.

- [ ] **Step 7: Run tests and probe**

Run: `python -m unittest discover -s tools/tests -p 'test_visual_test.py' -v`

Run: `python tools/visual_test.py probe`

Expected: all tests pass and probe emits one valid JSON object.

- [ ] **Step 8: Commit**

Commit the CLI, tests, and `artifacts/visual/` ignore rule with message
`feat: add JSON visual test runner`.

---

### Task 3: Developer commands and documentation

**Files:**
- Modify: `Makefile`
- Create: `tools/renderdoc-mcp.example.toml`
- Create: `docs/renderdoc-ai-testing.md`

**Interfaces:**
- Consumes: Task 2 CLI.
- Produces: `make visual-test`, `make visual-bench`, and
  `make renderdoc-capture`.

- [ ] **Step 1: Add Make entry points**

Each target depends on `build-game` and invokes the corresponding Python
subcommand with `BUILD_DIR`, `ARTIFACT_DIR`, `SEED`, `PRESET`, `MAP`, `FRAME`,
and `FIXED_DT` mapped only when set. Add concise `make help` descriptions.

- [ ] **Step 2: Add a portable MCP example**

Document the required stdio command, `PYTHONPATH`/`RENDERDOC_MODULE_PATH`, and
absolute-path placeholders. Mark it as an example that must be adapted rather
than executable configuration.

- [ ] **Step 3: Write operator documentation**

Cover prerequisites, probe output, all Make/Python commands, JSON schema,
artifact layout, global Codex registration, MCP capture inspection flow,
hardware/Xvfb/llvmpipe selection, timeouts, and actionable troubleshooting.

- [ ] **Step 4: Verify command surfaces**

Run: `make help`

Run: `python tools/visual_test.py --help`

Expected: all three Make targets and all six Python operations appear.

- [ ] **Step 5: Commit**

Commit these files with message `docs: add RenderDoc visual testing workflow`.

---

### Task 4: Global RenderDoc MCP installation

**Files:**
- External: `/home/sektant1/renderdoc-mcp`
- External: `/home/sektant1/.codex/config.toml`

**Interfaces:**
- Consumes: local RenderDoc 1.45 Python module and the third-party
  `renderdoc-mcp` stdio package already present or explicitly installed.
- Produces: global Codex MCP server named `renderdoc`.

- [ ] **Step 1: Inspect before changing global state**

Run `codex mcp get renderdoc`, inspect `/home/sektant1/renderdoc-mcp`, and verify
the server's declared Python compatibility, entry point, and tests. Do not
replace a working installation.

- [ ] **Step 2: Repair/install the isolated environment if required**

Use a compatible interpreter, install the local package editable without
silently fetching unrelated dependencies, and point `RENDERDOC_MODULE_PATH` at
the RenderDoc 1.45 Python module directory.

- [ ] **Step 3: Register idempotently**

Remove only a confirmed broken `renderdoc` entry, then run `codex mcp add` with
the absolute interpreter/module command and required environment. Re-read the
entry with `codex mcp get renderdoc`.

- [ ] **Step 4: Verify protocol startup**

Launch the exact configured stdio command, send MCP `initialize`,
`notifications/initialized`, and `tools/list` messages, and verify valid JSON-RPC
responses plus RenderDoc inspection tools.

---

### Task 5: End-to-end visual and GPU verification

**Files:**
- Generated: `artifacts/visual/**`
- Modify only if verification exposes a scoped integration defect.

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces: one validated screenshot, benchmark JSON, `.rdc`, and MCP inspection
  transcript/results.

- [ ] **Step 1: Run unit and focused C++ tests**

Run: `python -m unittest discover -s tools/tests -v`

Run: `cmake --build build --target frame_capture_tests game -j2`

Run: `build/frame_capture_tests`

Expected: all pass.

- [ ] **Step 2: Run an actual screenshot and benchmark**

Run: `make visual-test FRAME=90`

Run: `make visual-bench BENCH=120`

Validate their JSON and nonempty artifacts with
`python tools/visual_test.py validate`.

- [ ] **Step 3: Capture an actual frame**

Run: `make renderdoc-capture FRAME=90`

Expected: success JSON names one validated nonempty `.rdc`.

- [ ] **Step 4: Inspect the capture through MCP**

Open the latest `.rdc`; enumerate actions, query GL pipeline state and
resources, and retrieve a representative color target preview or pixel. Record
tool failures as defects rather than claiming MCP success from startup alone.

- [ ] **Step 5: Final repository checks**

Run: `git diff --check`

Run: `git status --short`

Confirm only intended new changes are staged/committed and all pre-existing
unrelated modifications remain intact.
