# RHI seam, module contracts, and removing OGRE {#doc-design-2026-07-29-rhi-and-module-contracts}

Status: accepted, not started
Date: 2026-07-29

## Decision

The engine gets a real RHI: a device-level contract (buffers, textures,
shaders, pipelines, command lists, swapchain) with swappable backends, and an
engine-owned layer above it that does scene graph, culling, materials, render
graph, shadows and particles. OGRE leaves the project.

Every engine module gets an interface with its implementation hidden behind it,
so an implementation can be replaced without touching a caller.

This is a renderer rewrite. It is worth writing down what that actually means
before starting, because the intermediate states are where projects die.

## What OGRE is doing today

Not "drawing triangles". Counted from `engine/src`:

| OGRE subsystem | What it gives us | Who replaces it |
|---|---|---|
| `SceneManager` / `SceneNode` (45 uses) | scene graph, transforms, visibility, culling | engine `render/` — partly exists as `SceneRegistry` |
| `MaterialManager` / `Technique` / `Pass` (27) | `.material` script format, parameter binding, per-pass state | engine material system + a format we own |
| `CompositorManager` (6) | the whole PSX post chain (`psx.compositor`: MRT, bloom, dither, resolve) | engine render graph |
| `ParticleSystem` / emitters / affectors (14) | particle simulation and batching | engine particle system — descriptors already ours (`ParticleEffectDesc`) |
| `MeshManager` / `Entity` / `SubEntity` (24) | mesh resources, draw submission, per-submesh materials | RHI buffers + engine draw list |
| `TextureManager` (6) | texture upload, mipmaps, format handling | RHI textures + an asset pipeline step |
| `Light` (6) | light collection, the 16-slot binding psx_lighting.glsl expects | engine lighting |
| `Camera` / `Viewport` (8) | view/projection, aspect, frustum | engine camera — trivial |
| stencil shadows | `SHADOWTYPE_STENCIL_MODULATIVE` | engine shadows — and see the note below |

Two things follow. First, "port the renderer to Vulkan" is really "write the
80% of a renderer that OGRE was doing". Second, the shadow question resolves
itself on the way: stencil shadow volumes cost ~2ms of a 10ms fullscreen frame
today and scale with real mesh complexity, so the replacement should be shadow
maps, not a reimplementation of what is there.

## Staging

Each milestone ends with the game running and the tests green. Nothing is
deleted until its replacement is proven.

**M1 — RHI contract and backend slots.** `eng/rhi/`: `Device`, `Swapchain`,
`Buffer`, `Texture`, `Shader`, `Pipeline`, `CommandList`. Handle-based, POD
descriptors, no GL or Vulkan types in the headers. `engine/src/rhi/gl/` and
`engine/src/rhi/vulkan/` exist as skeletons that satisfy the contract and fail
loudly when created: they are there to be filled in by hand, later, without
touching anything above them.

The engine keeps rendering through the existing OGRE path for the whole of M1.
The RHI is a plug point, not yet a dependency -- nothing above it calls it
until a backend can actually draw. That is deliberate: an RHI written against
no working backend is a guess, and the contract only earns trust once one real
frame has gone through it.

**M2 — engine scene layer.** Scene graph, transform hierarchy, frustum culling,
draw-list building. `SceneRegistry` already mirrors the graph for tooling and
becomes the source of truth. Proof: the same sample renders a loaded level.

**M3 — materials and shaders.** Own the material format (the current
`.material` files are OGRE script). Parameter binding, texture units, the PSX
shader set. Proof: the sample renders the dungeon with its real materials, and
`assetlint` validates the new format.

**M4 — render graph.** Replace the compositor chain: MRT, bloom, dither,
hardware resolve, at the 1/3 internal resolution the look depends on. Proof:
screenshots match the OGRE path frame-for-frame within tolerance.

**M5 — cut over.** Particles, lights, shadow maps, then the game switches to
the new path and OGRE is removed from the build. Proof: `game` and `psx_demo`
run, 51+ tests green, frame budget no worse than today (p50 10.1ms fullscreen).

**M6 — Vulkan backend.** `rhi/vulkan/` against the M1 contract. Proof: the
same scenes, selected by `--render-backend=vulkan`.

M1 is the one that is worth starting immediately, and the one that teaches
Vulkan later, because the contract it defines is what the Vulkan backend
implements. M2–M4 are the bulk of the work.

## Risks worth stating up front

- **The look is the product.** The PSX pipeline (1/3-res render, dither,
  affine-ish shading, 16-light binding) is the game's identity. Every milestone
  needs a screenshot comparison, not just "it renders".
- **The intermediate states are long.** M2–M4 are weeks where the new path is
  behind the old one. Keeping both alive is the cost of not breaking the game.
- **RHI contracts age badly if written speculatively.** M1's interface should
  be shaped by getting one real frame through GL, not by enumerating what
  Vulkan might want.

## Module contracts

All engine modules get an interface with the implementation hidden:
`IRenderer`, `IPhysics`, `IAudio`, `IInput`. Two caveats, both worth knowing
rather than discovering:

- The public headers already leak nothing: no Jolt, miniaudio, SDL or OGRE type
  appears in `engine/include`. The contract is already stable; what interfaces
  add is the ability to *swap* an implementation and to substitute fakes in
  tests.
- Virtual dispatch on per-body, per-frame physics calls is a real cost. The
  interface should be coarse -- `step()`, `createBody()`, batch queries -- not
  per-body accessors called in a loop.

## Physics: what is actually wrong

Findings from the code, to be confirmed against how it feels in play:

1. **Non-deterministic by default.** `Physics::init` uses
   `hardware_concurrency() - 1` worker threads unless `RAVEN_SCREENSHOT` or
   `RAVEN_FIXED_DT` is set, so contact resolution order varies run to run. For a
   game with replays, boss-fight retries or any future netcode, the simulation
   should be deterministic in normal play, not only under capture.
2. **Character sweeps cannot be filtered.** `characterUpdate` passes empty
   shape and body filters, so gameplay cannot exclude a body from the character
   controller's own sweep. Nothing can be made pass-through for the player.
3. **Feel constants are buried in the engine.** The character-vs-prop push
   impulse is `into * 2.0f` inside `CharacterPushListener`, and gravity is
   `-18.0f` hard-coded in `init`. Both are tuning, and tuning belongs in data.
4. **The controller is a bare `CharacterVirtual`.** No coyote time, no jump
   buffering, no landing recovery, no crouch/slide, no step smoothing. A fast
   soulslike FPS lives on exactly these; none of them exist yet.
5. **`Physics.cpp` is 900 lines** covering bodies, characters, queries, contact
   routing and debug drawing. Split along the same lines as the renderer.

Points 1 and 3 are unambiguous defects. Point 4 is a design gap, and which
parts matter depends on the movement model, which is a game decision.
