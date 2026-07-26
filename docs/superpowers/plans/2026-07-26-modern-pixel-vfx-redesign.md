# Modern Pixel VFX Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver larger, denser, more saturated and polished particles plus dedicated modern-pixel enchantment, portal, and liquid rendering with corrected supporting meshes.

**Architecture:** Keep Ogre billboard particles, but give each family a distinct pixel mask, saturated palette, and controlled emissive core. Keep enchantments as recursive material overlays, and replace generic sprite use for portals and liquids with dedicated GLSL programs. Express portal and pool geometry through pure, unit-testable composition/primitive geometry before attaching it to Ogre scene nodes.

**Tech Stack:** C++20, GLM, Ogre 14 GL3Plus, GLSL 330, CMake/CTest, RenderDoc 1.45, repository `tools/visual_test.py`.

## Global Constraints

- Preserve existing dirty-worktree changes and do not rewrite unrelated world shaders or models.
- Favor clean modern 3D pixel art over strict PS1 hardware simulation.
- Prioritize visual quality over the current llvmpipe performance baseline; measure and report performance without using it as the acceptance gate.
- Keep magnified pixel masks crisp with nearest filtering and stable mip behavior.
- Keep saturated high-chroma particle hues visible under bloom by separating dark bodies, colored highlights, and small emissive cores.
- Preserve registered particle presets as immutable defaults and reset every pooled system on checkout.
- Do not reintroduce a generic ring primitive or smooth torus portal mesh.
- Add arcane motes, frost shards, toxic bubbles, and portal wisps only as distinct reusable effects; do not add redundant variants.
- Use tests before production changes and verify each red failure is caused by the missing behavior.

---

### Task 1: Modern-pixel VFX asset contracts

**Files:**
- Modify: `engine/tests/ParticleAssetTests.cpp`
- Create: `engine/tests/VfxShaderAssetTests.cpp`
- Modify: `CMakeLists.txt`
- Create later in this task: `engine/assets/programs/vfx.program`
- Create later in this task: `engine/assets/shaders/portal.vert`
- Create later in this task: `engine/assets/shaders/portal.frag`
- Create later in this task: `engine/assets/shaders/liquid.vert`
- Create later in this task: `engine/assets/shaders/liquid.frag`

**Interfaces:**
- Consumes: repository asset files and `PROJECT_SOURCE_DIR`.
- Produces: `vfx_shader_asset_tests`; shader programs `PixelVfx/PortalVS`, `PixelVfx/PortalFS`, `PixelVfx/LiquidVS`, and `PixelVfx/LiquidFS`.

- [ ] **Step 1: Add failing asset-contract tests**

Add `VfxShaderAssetTests.cpp` with the existing `read()`/`requireText()` style.
Require:

```cpp
requireText(program, "vertex_program PixelVfx/PortalVS glsl",
            "dedicated portal vertex program is missing");
requireText(program, "fragment_program PixelVfx/PortalFS glsl",
            "dedicated portal fragment program is missing");
requireText(program, "vertex_program PixelVfx/LiquidVS glsl",
            "dedicated liquid vertex program is missing");
requireText(program, "fragment_program PixelVfx/LiquidFS glsl",
            "dedicated liquid fragment program is missing");
requireText(portalFragment, "floor(time * portalStepFps)",
            "portal animation is not frame-stepped");
requireText(portalFragment, "portalPalette",
            "portal output is not palette quantized");
requireText(liquidFragment, "floor(time * liquidStepFps)",
            "liquid animation is not frame-stepped");
requireText(liquidFragment, "liquidPalette",
            "liquid output is not palette quantized");
```

Require both materials later referenced by game assets to use their dedicated
programs, use `filtering none`, and declare explicit depth/blend state.

- [ ] **Step 2: Register and run the failing test**

Add:

```cmake
add_executable(vfx_shader_asset_tests engine/tests/VfxShaderAssetTests.cpp)
target_compile_definitions(
  vfx_shader_asset_tests PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME vfx_shader_assets COMMAND vfx_shader_asset_tests)
```

Run:

```bash
cmake --build build --target vfx_shader_asset_tests -j2
build/vfx_shader_asset_tests
```

Expected: FAIL because `vfx.program` and the dedicated shaders do not exist.

- [ ] **Step 3: Add minimal compiling shader programs**

Create four GLSL 330 shaders. Both vertex shaders consume `vertex`, `uv0`,
`worldViewProj`, and `time`, pass UV and world-independent animation time, and
write `gl_Position`. Portal fragment uniforms:

```glsl
uniform sampler2D portalTexture;
uniform vec4 portalDark;
uniform vec4 portalMid;
uniform vec4 portalBright;
uniform float portalStepFps;
uniform float portalFlowSpeed;
uniform float portalPixelGrid;
```

Liquid fragment uniforms:

```glsl
uniform sampler2D liquidTexture;
uniform vec4 liquidDark;
uniform vec4 liquidMid;
uniform vec4 liquidBright;
uniform vec2 liquidFlowA;
uniform vec2 liquidFlowB;
uniform float liquidStepFps;
uniform float liquidPixelGrid;
uniform float liquidEmission;
```

Use stepped time, quantized UVs, two or three palette bands, and no derivatives,
random temporal noise, or realistic transparency. Declare the four programs in
`vfx.program` with defaults and automatic time binding.

- [ ] **Step 4: Run asset contract and GLSL validation**

Run:

```bash
cmake --build build --target vfx_shader_asset_tests -j2
build/vfx_shader_asset_tests
glslangValidator -S vert engine/assets/shaders/portal.vert
glslangValidator -S frag engine/assets/shaders/portal.frag
glslangValidator -S vert engine/assets/shaders/liquid.vert
glslangValidator -S frag engine/assets/shaders/liquid.frag
```

Expected: all PASS.

- [ ] **Step 5: Commit the shader foundation**

Commit only the new test, CMake registration, program, and four shaders with
message `feat: add dedicated pixel VFX shader programs`.

---

### Task 2: Larger, denser, saturated particle vocabulary

**Files:**
- Modify: `engine/include/eng/particles/ParticlePresets.h`
- Modify: `engine/src/particles/ParticlePresets.cpp`
- Modify: `engine/assets/programs/psx.program`
- Modify: `engine/assets/materials/psx.material`
- Modify: `engine/assets/shaders/particle.frag`
- Modify: `engine/tests/ParticleAssetTests.cpp`
- Modify: `game/assets/lobby_showcase.toml`
- Modify: `game/src/ParticleEffects.cpp`
- Modify: `game/src/Spells.cpp`

**Interfaces:**
- Consumes: `ParticleEffectDesc`, `ParticleSpawnOptions`, Ogre billboard renderer.
- Produces: preset names `engine.arcane_motes`, `engine.frost_shards`, `engine.toxic_bubbles`, and `engine.portal_wisps`; procedural defines `PROCEDURAL_MOTE`, `PROCEDURAL_SHARD`, `PROCEDURAL_BUBBLE`, and `PROCEDURAL_WISP`.

- [ ] **Step 1: Extend particle tests with quality budgets**

In `ParticleAssetTests.cpp`, require the new preset names and distinct material
definitions. Require the shader defines above and enforce:

```cpp
requireText(presets, "base(Fire, \"Engine/Particles/Fire\", 0.22f, 0.27f",
            "fire particles were not enlarged");
requireText(presets, "0.20f, 0.26f, 72",
            "poison particles lack the quality-first size/quota");
requireText(showcase, "amount_scale = 3.0",
            "hero showcase particles are not visibly denser");
requireText(materials, "material Engine/Particles/PortalWisp",
            "portal wisps lack a dedicated material");
```

Also require saturated ramp components: poison green at least `1.25`, frost
cyan/blue at least `1.20`, fire red at least `1.20`, and arcane blue/magenta at
least `1.15`. Values above one are intentional HDR emission inputs.

- [ ] **Step 2: Run the failing particle test**

Run:

```bash
cmake --build build --target particle_asset_tests -j2
build/particle_asset_tests
```

Expected: FAIL on the first missing new preset or quality budget.

- [ ] **Step 3: Implement distinct procedural masks and materials**

Add four branches to `particle.frag`:

- Mote: compact asymmetric 5x5 diamond with a one-cell core.
- Shard: narrow 5x7 faceted crystal with a hard pointed end.
- Bubble: hollow 7x7 stepped circle with one bright corner cell.
- Wisp: curved 7x9 comma/flame mask with a smaller emissive head.

Use `floor(particleUV * grid)` and `step` operations so enlarged particles stay
crisp. Set alpha to zero outside each mask and keep at most one soft alpha band
for wisps. Register distinct fragment programs and materials. Use alpha blend
for bubbles, `src_alpha one` for motes/shards/wisps, `depth_write off`, and
nearest/clamped sampling.

- [ ] **Step 4: Increase default size, density, variation, and saturation**

Adjust current defaults without changing the descriptor API:

- Fire: approximately 1.4x base area, quota 64, rate 26.
- Smoke: approximately 1.35x base size, quota 64, rate 11.
- Poison: base `0.20 x 0.26`, quota 72, rate 22.
- Lava ash: base `0.14 x 0.14`, quota 64, rate 8.
- Hit sparks and pickup bursts: 1.25x size and 1.5x burst counts.

Keep dark saturated body colors beside HDR highlights. Add four presets with
different acceleration, direction cones, lifetime/velocity ranges, size ramps,
and saturated color ramps. Portal wisps drift inward/upward and have longer
lived bodies; frost shards fall or burst with narrow angular spread; toxic
bubbles rise slowly; arcane motes orbit visually through broad lateral spread
without adding a simulation subsystem.

- [ ] **Step 5: Author showcase and gameplay usage**

Raise hero `amount_scale` values into the 2.5–3.5 range, size scales into the
1.7–2.3 range, and tune emitter radius so cards do not form one opaque cloud.
Attach portal wisps to down/up portals through their existing style particle
name. Use frost shards and arcane motes in existing relevant spell/enchantment
presentation points only; do not add new gameplay damage or status behavior.

- [ ] **Step 6: Verify particle tests and build**

Run:

```bash
cmake --build build --target particle_asset_tests particle_options_tests game -j2
build/particle_asset_tests
build/particle_options_tests
```

Expected: PASS and no shader/material load errors in the game build.

- [ ] **Step 7: Commit particle redesign**

Commit the files above with message
`feat: enlarge and saturate modern pixel particles`.

---

### Task 3: Pixel-banded enchantment overlay

**Files:**
- Modify: `engine/tests/EnchantmentTests.cpp`
- Modify: `engine/assets/shaders/enchantment.frag`
- Modify: `engine/assets/programs/enchantment.program`
- Modify: `engine/src/Renderer.cpp`
- Modify: `engine/include/eng/render/Enchantment.h`

**Interfaces:**
- Consumes: `EnchantmentDesc`, recursive material bookkeeping, object-space position/normal.
- Produces: uniforms `enchantBandCount`, `enchantPixelScale`, and `enchantCoreBoost`.

- [ ] **Step 1: Add failing descriptor and shader tests**

Add defaults and sanitization assertions:

```cpp
require(desc.bandCount == 4.0f, "default enchant band count changed");
require(desc.pixelScale == 18.0f, "default enchant pixel scale changed");
require(desc.coreBoost == 1.35f, "default enchant core boost changed");
```

Require the fragment shader to quantize position/intensity using `floor`, define
`quantizeBand`, and separate `runeBody` from `runeCore`. Require renderer
bindings for all three new uniforms and explicit additive blend, depth-check on,
depth-write off, lighting off, and cull none on the generated overlay pass.

- [ ] **Step 2: Run the failing enchantment test**

Run:

```bash
cmake --build build --target enchantment_tests -j2
build/enchantment_tests
```

Expected: FAIL on missing descriptor fields.

- [ ] **Step 3: Implement descriptor fields and renderer state**

Add the three finite/clamped fields:

- `bandCount`: 2–8.
- `pixelScale`: 4–64.
- `coreBoost`: 0–3.

Bind them in `Renderer::setNodeEnchantment`. Configure the overlay pass
explicitly rather than relying on cloned base-pass state.

- [ ] **Step 4: Implement chunky runes and controlled bloom**

Quantize triplanar coordinates before `runeField`, replace smooth thin rune
edges with grid-cell occupancy plus a one-cell core, quantize pulse/intensity
into `bandCount`, and apply `coreBoost` only to the core. Preserve
`cameraPositionObject` and a banded Fresnel edge.

- [ ] **Step 5: Validate and test**

Run:

```bash
glslangValidator -S vert engine/assets/shaders/enchantment.vert
glslangValidator -S frag engine/assets/shaders/enchantment.frag
cmake --build build --target enchantment_tests game -j2
build/enchantment_tests
```

Expected: PASS.

- [ ] **Step 6: Commit enchantment redesign**

Commit with message `feat: pixel-band enchantment overlays`.

---

### Task 4: Correct stepped portal mesh and dedicated membrane

**Files:**
- Modify: `game/src/SceneFactory.h`
- Modify: `game/src/SceneFactory.cpp`
- Create: `game/src/PortalGeometry.h`
- Create: `game/src/PortalGeometry.cpp`
- Create: `game/tests/PortalGeometryTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `game/assets/materials/game.material`
- Modify: `game/src/LiveLevel.cpp`

**Interfaces:**
- Consumes: `PrimitiveMeshDesc`, `PortalPropStyle`, dedicated portal programs.
- Produces: `std::vector<PortalBlock> buildSteppedPortalBlocks(const PortalGeometryDesc&)`, where `PortalBlock` contains local `position` and `scale`.

- [ ] **Step 1: Write failing pure portal-geometry tests**

Test a default descriptor for:

- Two full-height pillars.
- Two shoulder blocks and one lintel/keystone layer.
- Symmetry across local X.
- Positive finite scales.
- Opening width/height preserved within `0.001`.
- Every block depth equals the descriptor frame depth.
- Invalid radius, width, height, bevel, or non-finite values return no blocks.

Also extend `VfxShaderAssetTests.cpp` to require `Game/PortalDown` and
`Game/PortalUp` to reference `PixelVfx/PortalVS`/`PortalFS`, nearest sampling,
depth write on, and culling none.

- [ ] **Step 2: Register and run failing tests**

Create `portal_geometry_tests` from the test and `PortalGeometry.cpp`, link GLM,
then run:

```bash
cmake --build build --target portal_geometry_tests vfx_shader_asset_tests -j2
build/portal_geometry_tests
build/vfx_shader_asset_tests
```

Expected: FAIL before the pure composition API and material migration exist.

- [ ] **Step 3: Implement the pure stepped-arch composition**

Return mirrored pillar/shoulder transforms and a centered lintel/keystone
transform. Keep the opening unobstructed and avoid parent-scale multiplication:
frame blocks receive their final local dimensions directly; the frame parent
remains unit scale.

- [ ] **Step 4: Migrate `createPortalProp`**

Use one shared beveled-box mesh and instantiate the returned transforms. Build
the membrane from a `Disc` with 12–16 segments, orient its normal toward the
walkable approach, and scale X/Y in the membrane's local plane only. Retain
labels, interaction position, light, particle attachment, and cleanup behavior.

- [ ] **Step 5: Migrate portal materials**

Use the dedicated portal shader with separate saturated descent
green/chartreuse and ascent blue/cyan palettes. Configure a small bright band
above bloom threshold and darker bands below it. Attach `engine.portal_wisps`
unless a portal style explicitly overrides particles.

- [ ] **Step 6: Run tests and game build**

Run:

```bash
cmake --build build --target portal_geometry_tests vfx_shader_asset_tests primitive_tests game -j2
build/portal_geometry_tests
build/vfx_shader_asset_tests
build/primitive_tests
```

Expected: PASS.

- [ ] **Step 7: Commit portal redesign**

Commit with message `feat: build stepped pixel-art portals`.

---

### Task 5: Dedicated pixel-art liquids and corrected surfaces

**Files:**
- Modify: `engine/tests/VfxShaderAssetTests.cpp`
- Create: `game/src/LiquidSurface.h`
- Create: `game/src/LiquidSurface.cpp`
- Create: `game/tests/LiquidSurfaceTests.cpp`
- Modify: `game/src/SceneFactory.cpp`
- Modify: `game/assets/materials/fantasy.material`
- Modify: `game/assets/lobby_showcase.toml`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PrimitiveMeshDesc`, dedicated liquid program.
- Produces: `LiquidSurfacePlacement resolveLiquidSurface(const LiquidSurfaceDesc&)`, containing local position, orientation, and scale.

- [ ] **Step 1: Write failing surface-placement and material tests**

Test that a pool surface:

- Is horizontal with local normal +Y after orientation.
- Is recessed below the rim by the descriptor amount.
- Uses X/Z dimensions without inheriting plinth Y scale.
- Rejects zero, negative, or non-finite dimensions/recess.

Require `Fantasy/Water`, `Fantasy/Lava`, and `Fantasy/ToxicSlime` to use
`PixelVfx/LiquidVS`/`LiquidFS`, explicit opaque depth state, nearest magnification,
and distinct three-band palettes. Require water emission `0`, lava emission
greater than `1`, and slime emission between them.

- [ ] **Step 2: Register and run failing tests**

Run:

```bash
cmake --build build --target liquid_surface_tests vfx_shader_asset_tests -j2
build/liquid_surface_tests
build/vfx_shader_asset_tests
```

Expected: FAIL before placement API and material migration.

- [ ] **Step 3: Implement pure placement and scene composition**

Resolve a plane/disc surface transform independently from the plinth scale.
Create pool exhibits as one plinth node plus a sibling surface node, rather than
scaling the liquid beneath a nonuniformly scaled parent. Use 0.01–0.04 world
units of recess to avoid z-fighting without visibly burying the surface.

- [ ] **Step 4: Migrate liquid materials**

Give water deep blue/cyan/white-blue bands with no HDR body, lava
red/orange/yellow bands with a small HDR highlight, and slime
deep green/chartreuse/yellow-green bands with a restrained HDR highlight.
Configure dual stepped flows with different directions and rates.

- [ ] **Step 5: Validate and test**

Run:

```bash
glslangValidator -S vert engine/assets/shaders/liquid.vert
glslangValidator -S frag engine/assets/shaders/liquid.frag
cmake --build build --target liquid_surface_tests vfx_shader_asset_tests particle_asset_tests game -j2
build/liquid_surface_tests
build/vfx_shader_asset_tests
build/particle_asset_tests
```

Expected: PASS.

- [ ] **Step 6: Commit liquid redesign**

Commit with message `feat: add modern pixel liquid surfaces`.

---

### Task 6: RenderDoc replay diagnosis and representative draw inspection

**Files:**
- Modify only if evidence requires it: `tools/visual_test.py`
- Modify only if evidence requires it: `engine/src/render/FrameCapture.cpp`
- Modify only if stable regression coverage is possible: `tools/tests/test_visual_test.py`
- Record results: `docs/renderdoc-ai-testing.md`

**Interfaces:**
- Consumes: engine screenshot/capture commands and RenderDoc MCP.
- Produces: replayable `.rdc` or a precise documented external replay limitation.

- [ ] **Step 1: Reproduce both visual paths**

Run the deterministic screenshot and exact-frame capture at frame 90, seed 1,
PS1 preset. Record the absolute PNG/RDC paths and inspect `process.log` for the
actual RenderSystem and GL context.

- [ ] **Step 2: Isolate the replay-context mismatch**

Compare capture metadata, engine GL3Plus selection, Xvfb GLX availability, and
MCP replay environment. Test one variable at a time: software-rendering flag,
display selection, and replay module path. Do not alter shader code to address a
replay-host context problem.

- [ ] **Step 3: Add a failing tooling test only if repository code is causal**

If command/environment construction selects the wrong API, encode the expected
OpenGL/GLX environment in `test_visual_test.py`, run the focused test to observe
failure, then minimally fix `visual_test.py`. If repository code is not causal,
make no tooling change.

- [ ] **Step 4: Capture and inspect representative draws**

Open the new capture through MCP. Find transparent/additive draws, then inspect
at least one particle, one enchantment, one portal, and one liquid draw using
draw state, shader bindings/reflection, and post-VS data. Export a portal or
liquid mesh if event data is available. Confirm expected blend/depth/cull,
texture, shader, vertex count, normals/UVs, and transformed orientation.

- [ ] **Step 5: Document evidence**

Append commands, capture path, API/context, representative event IDs, and any
external replay limitation to `docs/renderdoc-ai-testing.md`. Commit a tooling
fix only if tests demonstrate it; otherwise commit documentation with message
`docs: record modern pixel VFX RenderDoc inspection`.

---

### Task 7: Visual polish, regression suite, and measured handoff

**Files:**
- Modify only measured tuning values in files changed by Tasks 2–5.
- Update: `docs/superpowers/plans/2026-07-26-modern-pixel-vfx-redesign.md`

**Interfaces:**
- Consumes: all prior VFX work and deterministic visual tooling.
- Produces: verified final visuals and recorded measurements.

- [ ] **Step 1: Run focused regression suite**

Run:

```bash
cmake --build build --target \
  particle_asset_tests particle_options_tests enchantment_tests \
  primitive_tests portal_geometry_tests liquid_surface_tests \
  vfx_shader_asset_tests game -j2
build/particle_asset_tests
build/particle_options_tests
build/enchantment_tests
build/primitive_tests
build/portal_geometry_tests
build/liquid_surface_tests
build/vfx_shader_asset_tests
git diff --check
```

Expected: all PASS and no whitespace errors.

- [ ] **Step 2: Produce deterministic after screenshot**

Run:

```bash
make visual-test FRAME=90 SEED=1 PRESET=ps1
```

Compare with the baseline screenshot from the design investigation. Inspect
particle masks, saturated hue retention, bloom shape, portal silhouette,
enchantment readability, liquid orientation, z-fighting, and label occlusion.

- [ ] **Step 3: Tune one diagnosed issue at a time**

For each visible defect, state one hypothesis, change one group of related
values, rerun its focused test, and produce another frame-90 screenshot. Stop
after the four effect families meet the acceptance criteria; do not refactor
unrelated rendering.

- [ ] **Step 4: Measure the quality-first result**

Run:

```bash
make visual-bench BENCH=120
make renderdoc-capture FRAME=90 SEED=1 PRESET=ps1
```

Record frame time, draw calls, triangles, capture size, and whether MCP replay
succeeds. Performance regressions are reported, not silently optimized by
reducing the requested size, density, bloom, saturation, or polish.

- [ ] **Step 5: Final verification**

Run:

```bash
ctest --test-dir build --output-on-failure
git diff --check
git status --short
```

Expected: all tests pass. Report any pre-existing failures separately with
their exact output.

- [ ] **Step 6: Commit final tuning**

If visual tuning changed tracked files after the component commits, commit them
with message `fix: polish modern pixel VFX presentation`.
