# Particle Overrides, Generic Enchantment, and Model Setup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-spawn particle customization, model-independent recursive enchantments, easy render/collider model spawning, and fix the lobby particle/label presentation.

**Architecture:** Registered particle and enchant presets remain immutable defaults; runtime instances receive sanitized descriptors when spawned/applied. Model setup is a thin engine helper over Renderer and Physics, using engine-owned mesh metadata for automatic colliders.

**Tech Stack:** C++20, GLM, Ogre rendering, Jolt physics, GLSL 330, toml++ and CTest.

## Global Constraints

- Existing particle spawn and enchantment calls remain source-compatible.
- Model import may intentionally replace legacy pivot/unit/material behavior;
  migrate in-repository callers instead of preserving inconsistent conventions.
- Pooled particle overrides must never leak into later spawns.
- Enchantments preserve every submesh's base material and work without usable UVs.
- Dynamic triangle-mesh colliders are rejected; dynamic defaults use primitive colliders.
- Existing user changes in the dirty worktree must be preserved.

---

### Task 1: Per-spawn particle options

**Files:**
- Modify: `engine/include/eng/particles/ParticleEffectDesc.h`
- Modify: `engine/src/particles/Particles.h`
- Modify: `engine/src/particles/Particles.cpp`
- Modify: `engine/include/eng/Renderer.h`
- Modify: `engine/src/Renderer.cpp`
- Create: `engine/tests/ParticleOptionsTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `ParticleSpawnOptions`, `sanitizeParticleSpawnOptions`, and Renderer/Particles spawn overloads accepting `const ParticleSpawnOptions&`.

- [ ] Write tests asserting identity defaults and sanitization of every scalar/tint.
- [ ] Run `cmake --build build --target particle_options_tests -j2 && build/particle_options_tests`; expect failure before the API exists.
- [ ] Add `ParticleSpawnOptions` with size, amount, lifetime, speed, box-radius, point-radius, tint, and local offset fields.
- [ ] Extract a pure sanitizer so invalid values are testable without Ogre.
- [ ] Pass options through Renderer to `Particles::spawn`.
- [ ] On every pool checkout, reapply dimensions, emitter type/volume, rate or burst window, TTL, velocity, and tint from preset × options.
- [ ] Store overridden maximum TTL for one-shot retirement.
- [ ] Run the focused test and particle asset test; expect both to pass.

### Task 2: Showcase particle options and label offsets

**Files:**
- Modify: `game/src/SceneFactory.h`
- Modify: `game/src/SceneFactory.cpp`
- Modify: `game/src/LiveLevel.cpp`
- Modify: `game/assets/lobby_showcase.toml`
- Modify: `engine/tests/ParticleAssetTests.cpp`

**Interfaces:**
- Consumes: `ParticleSpawnOptions` and Renderer spawn overloads from Task 1.
- Produces: TOML `particle_options` parsing and `ShowcaseExhibit::labelOffset`.

- [ ] Extend the content test to require nested poison/lava overrides and horizontal particle-altar label offsets.
- [ ] Run `build/particle_asset_tests`; expect failure on missing authored options.
- [ ] Parse nested scalar/tint/local-offset fields, with legacy `particle_offset` as fallback.
- [ ] Add bounds-derived anchor plus `labelOffset` in `LiveLevel`.
- [ ] Author larger/denser poison and lava ash overrides and inward horizontal plaque offsets for all particle altars.
- [ ] Run particle asset, lobby dressing, and level document tests; expect pass.

### Task 3: Generic recursive enchantments

**Files:**
- Create: `engine/include/eng/render/Enchantment.h`
- Modify: `engine/include/eng/Renderer.h`
- Modify: `engine/src/Renderer.cpp`
- Modify: `engine/src/RendererImpl.h`
- Modify: `engine/assets/shaders/enchantment.vert`
- Modify: `engine/assets/shaders/enchantment.frag`
- Modify: `engine/assets/programs/enchantment.program`
- Create: `engine/tests/EnchantmentTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `EnchantmentDesc`, recursive `setNodeEnchantment`, and recursive `clearNodeEnchantment`.

- [ ] Write pure descriptor sanitization tests and shader-contract tests requiring object-space/triplanar inputs.
- [ ] Run the new test; expect failure before the descriptor and shader contract exist.
- [ ] Add descriptor defaults and style palette resolution.
- [ ] Walk target descendants and apply/clear enchantment material clones without duplicate passes.
- [ ] Send object position, object normal, camera position, scroll, scale, pulse, edge, colour, and strength uniforms.
- [ ] Replace UV-only runes with quantized triplanar rune projection plus fresnel edge glow.
- [ ] Run C++ tests and `glslangValidator` for both shaders; expect pass.

### Task 4: Easy model render/collider setup

**Files:**
- Create: `engine/include/eng/Model.h`
- Create: `engine/src/render/Model.cpp`
- Modify: `engine/include/eng/Renderer.h`
- Modify: `engine/src/Renderer.cpp`
- Modify: `CMakeLists.txt`
- Create: `engine/tests/ModelTests.cpp`

**Interfaces:**
- Consumes: Renderer mesh loading/bounds, Physics body creation, and `EnchantmentDesc`.
- Produces: `ColliderMode`, `ModelDesc`, `ModelInstance`, `spawnModel`, and `destroyModel`.

- [ ] Write tests for scaled box/sphere bounds, no-collider setup, and dynamic static-mesh rejection.
- [ ] Run `build/model_tests`; expect failure before the model API exists.
- [ ] Expose plain-data mesh local bounds from Renderer.
- [ ] Implement pure collider derivation from bounds and nonuniform scale.
- [ ] Implement model node creation, mesh attachment, optional recursive enchantment, and primitive body creation.
- [ ] Implement static triangle collision using cached OBJ geometry; reject it when `dynamic=true`.
- [ ] Ensure failed body creation destroys the render node and cleanup invalidates returned handles.
- [ ] Run model and physics tests; expect pass.

### Task 5: Standardized model import

**Files:**
- Create: `engine/include/eng/render/ModelImport.h`
- Modify: `engine/include/eng/Renderer.h`
- Modify: `engine/src/Renderer.cpp`
- Modify: `engine/src/ObjLoader.h`
- Modify: `engine/src/ObjLoader.cpp`
- Modify: `engine/include/eng/Model.h`
- Create: `engine/tests/ModelImportTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: mesh bounds/collision geometry and `spawnModel` from Task 4.
- Produces: `ModelImportOptions`, `PivotMode`, stable import cache keys, and option-aware OBJ/model loading.

- [ ] Write failing pure tests for Source, BoundsCenter, BottomCenter, and Custom pivot transforms using asymmetric bounds.
- [ ] Test canonical metres/+Y-up/-Z-forward defaults, normal transforms, and rejection of non-finite/zero transforms.
- [ ] Test that transformed render positions, bounds, and collision positions use the identical canonical matrix.
- [ ] Add deterministic cache-key tests proving distinct pivot/custom transforms never alias and identical options do.
- [ ] Implement `ModelImportOptions` and pure sanitization/matrix/cache-key helpers.
- [ ] Route option-aware OBJ loading through one transformed geometry stream used for both Ogre mesh creation and collision capture.
- [ ] Migrate in-repository authored-pivot callers to explicit Source import;
  legacy import overloads may be removed when the cleaner API replaces them.
- [ ] Add default material fallback plus indexed submesh remapping to model setup; log and use the engine prototype material for missing names.
- [ ] Run model-import, model, OBJ-geometry, engine, and game tests.

### Task 6: Performance regression audit and optimization

**Files:**
- Modify: focused renderer/particle/showcase files identified by measurements
- Create: `engine/tests/RenderCostTests.cpp` if stable structural budgets can be tested
- Modify: `CMakeLists.txt` only when adding the focused test

**Interfaces:**
- Consumes all visual systems from Tasks 1–5.
- Produces measured feature-level cost evidence and optimized runtime behavior.

- [ ] Record the reported regression baseline: approximately 3 ms / 66 draw
  calls / 19k triangles before versus 9 ms / 172 draw calls / 42k triangles
  after in the lobby.
- [ ] Add or use existing feature toggles to isolate enchant passes, particles,
  labels, showcase composite props, shadows, and lobby dressing one at a time.
- [ ] Capture render time, draw calls, triangles, live particle counts, cloned
  enchant materials/passes, loaded meshes, and visible text sprites per case.
- [ ] Rank measured costs and state one root-cause hypothesis per optimization.
- [ ] Add structural regression tests for the dominant deterministic counts
  before changing production code.
- [ ] Optimize dominant causes using shared meshes/material variants, static
  batching/instancing where valid, visibility/range culling, particle quotas,
  and avoiding unnecessary enchant passes or shadow casters.
- [ ] Preserve visual identity and per-use customization; do not globally
  disable particles, enchantments, labels, or shadows.
- [ ] Rebuild and rerun focused tests after each isolated optimization.
- [ ] Report before/after draw-call, triangle, and render-time evidence. If the
  environment cannot open the graphical runtime, provide a reproducible
  benchmark command and mark user-side FPS confirmation explicitly pending.

### Task 7: Unified primitive shape API and ring removal

**Files:**
- Create: `engine/include/eng/Primitive.h`
- Create: `engine/src/render/Primitive.cpp`
- Modify: `engine/include/eng/Renderer.h`
- Modify: `engine/src/Renderer.cpp`
- Modify: `engine/src/ProceduralMeshes.h`
- Modify: `engine/src/ProceduralMeshes.cpp`
- Modify: `game/src/SceneFactory.cpp`
- Modify: affected game/demo authored content
- Create: `engine/tests/PrimitiveTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Renderer, Physics, enchant descriptors, and model-style instance
  ownership.
- Produces: one render/physics primitive descriptor and removes generic rings.

- [ ] Define `PrimitiveKind`: Box, BeveledBox, Sphere, Capsule, Cylinder, Cone,
  Plane, and Disc; do not include Ring.
- [ ] Define `PrimitiveDesc` with transform, dimensions, tessellation, material,
  shadows, optional enchantment, collider mode/layer/dynamics, and physical
  properties.
- [ ] Add pure validation plus mapping from each render shape to its default
  physics shape/dimensions; plane uses configurable thin box collision and cone
  uses a documented cylinder approximation unless collision is disabled.
- [ ] Add or consolidate procedural mesh generators so
  `Renderer::createPrimitiveMesh(desc)` is the only generic primitive entry.
- [ ] Add `spawnPrimitive(Renderer&, Physics&, desc)` and matching cleanup,
  returning node/mesh/body handles with the same ownership guarantees as models.
- [ ] Remove `createPortalRing`/generic ring generation and migrate every call:
  particle altars use cylinder/disc bowls and staffs use sphere/cone
  composition.
- [ ] Remove authored portal-arch mesh usage and its dedicated assets. Build
  portals from two beveled primitive pillars, a primitive lintel, and the
  membrane; no ring or arch mesh remains.
- [ ] Search the repository to prove no ring primitive/model/material use remains
  except unrelated prose/math terminology.
- [ ] Add tests covering mesh dispatch, collider mappings, validation, cleanup,
  and the repository ring-removal contract.
- [ ] Build engine/game and run primitive/model/physics/lobby tests.

### Task 8: Headless visual benchmark and RenderDoc AI tooling

**Files:**
- Create: `engine/include/eng/render/FrameCapture.h`
- Create: `engine/src/render/FrameCapture.cpp`
- Modify: `engine/src/Engine.cpp`
- Create: `tools/visual_test.py`
- Create: `tools/renderdoc-mcp.example.toml`
- Modify: `Makefile`
- Modify: `CMakeLists.txt`
- Create: `engine/tests/FrameCaptureTests.cpp`
- Create: `docs/renderdoc-ai-testing.md`

**Interfaces:**
- Consumes: screenshot/benchmark environment hooks and RenderDoc 1.45 API when
  available.
- Produces: deterministic headless run/capture commands and machine-readable
  JSON artifacts for AI CLI/MCP consumers.

- [ ] Add a probe that reports JSON capability state for display, GL/EGL,
  `renderdoccmd`, RenderDoc app API, screenshot output, and MCP CLI.
- [ ] Add a headless runner using `xvfb-run`/Xvfb with llvmpipe fallback,
  isolated temporary Xauthority/display selection, deterministic fixed timestep,
  and explicit actionable errors when the system dependency is missing.
- [ ] Add `make visual-test`, `make visual-bench`, and `make renderdoc-capture`
  targets that call the runner and write JSON metrics/screenshots/captures under
  a caller-selected artifact directory.
- [ ] Dynamically discover the RenderDoc in-application API and support
  `PSX_RENDERDOC_FRAME=<n>` plus `PSX_RENDERDOC_CAPTURE=<path>` without a hard
  runtime dependency. Start/end exactly one frame and log capture status.
- [ ] Wrap `renderdoccmd capture` with no-vsync/no-fullscreen, working directory,
  wait-for-exit, deterministic capture template, and benchmark/screenshot env.
- [ ] Provide an AI CLI JSON schema/commands for probe, benchmark, screenshot,
  capture, latest-artifact, and validation.
- [ ] Provide a RenderDoc MCP configuration example compatible with the
  third-party `renderdoc-mcp` stdio server, but keep it optional and never
  download/execute third-party binaries silently.
- [ ] Add tests for command construction, capability JSON, artifact discovery,
  and missing-dependency diagnostics.
- [ ] Run an actual headless screenshot/benchmark and RenderDoc capture in this
  environment after installing/locating Xvfb; verify nonempty PNG/JSON/RDC.

### Task 9: Integration verification

**Files:**
- Modify only files needed to resolve integration failures.

**Interfaces:**
- Consumes all prior tasks.
- Produces a buildable, tested engine/game integration.

- [ ] Build `game`, `particle_options_tests`, `particle_asset_tests`, `enchantment_tests`, and `model_tests`.
- [ ] Run focused CTest filters for particles, lobby, level documents, enchantments, models, and physics.
- [ ] Validate particle and enchantment GLSL variants with `glslangValidator`.
- [ ] Parse lobby TOML and assert poison/lava multipliers plus unique horizontal label offsets.
- [ ] Run `git diff --check`.
- [ ] Record that runtime visual confirmation still requires restarting the user's graphical game process.
