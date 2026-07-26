# Particle Overrides, Generic Enchantment, and Model Setup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-spawn particle customization, model-independent recursive enchantments, easy render/collider model spawning, and fix the lobby particle/label presentation.

**Architecture:** Registered particle and enchant presets remain immutable defaults; runtime instances receive sanitized descriptors when spawned/applied. Model setup is a thin engine helper over Renderer and Physics, using engine-owned mesh metadata for automatic colliders.

**Tech Stack:** C++20, GLM, Ogre rendering, Jolt physics, GLSL 330, toml++ and CTest.

## Global Constraints

- Existing particle spawn and enchantment calls remain source-compatible.
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

### Task 5: Integration verification

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
