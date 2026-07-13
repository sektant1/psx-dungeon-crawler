# Engine Scaffold Design — PSX-style FPS RPG Dungeon Crawler

**Date:** 2026-07-13
**Status:** Approved
**Scope:** Project scaffold / quick start only. Turns the `ogre-psx-demo` repo into
the skeleton of a C++ single-player FPS RPG dungeon crawler engine, with the
existing OGRE 14.5 + SDL2 PSX renderer as the engine's renderer module.

## Goals

1. Restructure the repo into an engine static library + game executable + sample.
2. Fully hide Ogre (and SDL) behind a public engine API — game code includes no
   Ogre or SDL headers.
3. Port the existing PSX demo scene to the public API as `samples/psx-demo`,
   proving the API covers real usage and serving as a renderer regression test.
4. Done-bar: `game` executable runs an FPS walk (WASD + mouse-look) in a simple
   textured test room with the PSX shaders and dither compositor active.

## Non-goals (deferred)

- ECS / game-object layer, audio, game-state stack, collision beyond room AABB
  clamp, fixed-timestep simulation, unit-test framework, save/load, UI.
- Swappable render backends. Ogre is *the* renderer; the abstraction exists for
  boundary hygiene, not backend portability.

## Decisions (from brainstorm interview)

| Topic | Decision |
|---|---|
| Demo fate | Survives, ported to public API as `samples/psx-demo` |
| Code split | Engine static lib (`eng`) + game exe + sample exe, one repo |
| Scaffold modules | Core, Platform, Input, Renderer only |
| Done-bar | FPS walk in PSX test room |
| Input | Action-mapped, bindings from TOML config |
| Renderer boundary | Full abstraction — no Ogre types in public headers |
| Config format | TOML (vendored toml++) |
| Sample vs API | Sample ported to the public API (API sized to cover it) |
| Math types | GLM (system package) in public API, converted to Ogre internally |

## Repo layout

```
CMakeLists.txt            top-level; Makefile wrapper (make run-game / run-demo)
engine/
  include/eng/            PUBLIC headers — zero Ogre/SDL includes
  src/                    implementation: Ogre + SDL live here only
  assets/                 psx shaders/materials/compositor/programs (engine-owned)
game/
  src/                    dungeon crawler exe (scaffold: FPS test room)
  assets/                 test room textures, game.toml (settings + bindings)
samples/psx-demo/
  src/                    demo main ported to public API
  assets/                 crystal/box meshes, demo textures, sparkle.particle
docs/superpowers/specs/   this document
third_party/              tomlplusplus (vendored single header)
tools/                    gltf_to_obj.py (unchanged)
```

The repo is not yet a git repository; scaffolding includes `git init` and an
initial commit of the current demo state before restructuring.

Asset split: PSX shader stack (shaders, programs, materials, compositor,
psxdither.png) is engine-owned under `engine/assets/`. Demo-specific meshes,
textures, and the particle script move to `samples/psx-demo/assets/`. Test-room
assets live under `game/assets/`. Asset roots are compiled in per target (as
today's `ASSET_DIR`); engine assets registered by the engine, target assets by
the target.

## Engine modules (static lib `eng`, namespace `eng`)

### Core

- **Log** — levels (debug/info/warn/error/fatal), stderr sink. Fatal logs then
  aborts the frame path via error return; no exceptions across the public API.
- **Config** — loads a TOML file (toml++), typed getters with defaults.
- **Time** — per-frame variable `dt` from `steady_clock`, clamped to 0.1 s max.
  Fixed-step simulation deferred until gameplay needs it.

### Platform

- SDL2 window creation (no `SDL_WINDOW_OPENGL`; Ogre GL3Plus builds its own GL
  context against the native X11 handle, exactly as the demo does today,
  including the `SDL_VIDEODRIVER=x11` Wayland workaround in the Makefile).
- Event pump: quit, resize (forwarded to renderer), key/mouse events (forwarded
  to Input). Owned and sequenced by `Engine`.

### Input

- Action map loaded from TOML: `[bindings]` table mapping action name → SDL key
  name string (e.g. `move_forward = "W"`). Unknown key names fail startup with
  an error listing valid names.
- Queries: `isDown(action)`, `wasPressed(action)` (edge-triggered per frame),
  `mouseDelta()` (relative, pixels/frame), `setMouseGrab(bool)` (relative mouse
  mode + cursor capture).

### Engine facade

`eng::Engine` owns lifetime and ordering:

```
Engine::init(configPath) -> bool     // SDL, window, Ogre Root, resources, input
Engine::pollEvents()                 // pump SDL, update Input, handle resize/quit
Engine::shouldClose() -> bool
Engine::renderFrame(dt)
Engine::shutdown()                   // Ogre Root destroyed before SDL window
```

Shutdown order preserved from the demo: Ogre first, native window after.

## Renderer public API

All public types in `engine/include/eng/`; math is GLM (`glm::vec3`,
`glm::quat`, `glm::mat4`), converted to Ogre types inside `engine/src/`.

Handles are opaque 32-bit ids into internal tables. Use of an invalid handle is
a fatal log. Creation functions return invalid handle + error log on failure
(missing file, unknown material).

```
// Meshes
MeshHandle loadObj(path, const glm::mat4* bake = nullptr)   // ObjLoader + optional vertex bake
MeshHandle createInteriorBox(size, subdivisions)            // ProceduralMeshes
MeshHandle createPlane(size)

// Scene graph
NodeHandle createNode(NodeHandle parent, glm::vec3 pos = {})
void setPosition(node, glm::vec3)
void setOrientation(node, glm::quat)          // or row-major basis overload
void setScale(node, glm::vec3)

// Attachments
void attachMesh(node, mesh, const std::string& materialName)
void attachParticles(node, const std::string& templateName)
void attachCamera(node)                       // engine owns the single camera
void attachLight(node, const LightDesc&)      // type (directional/point), colour, range

// Camera
void setCameraFov(degrees) / setCameraClip(near, far)

// Materials
void setMaterialParam(materialName, paramName, value)   // float / vec3 / vec4 overloads

// Environment
void setAmbient(colour)
void setFog(colour, expDensity)
void setBackground(colour)

// Post + verification
void setDitherEnabled(bool)
void writeScreenshot(path)
```

This surface covers 100 % of current `main.cpp` usage (crystal spire bakes via
`loadObj` bake matrix, per-material modulate/uv tweaks via `setMaterialParam`,
sparkles via `attachParticles`), so the demo port is mechanical. API grows only
when the game needs more.

## Game executable (scaffold done-bar)

- `FpsController` lives in `game/src/` (game code, not engine): mouse-look yaw
  on a body node + pitch on a head node (pitch clamped ±89°), WASD movement on
  the ground plane, speed from `game.toml`.
- Test room: `createInteriorBox` with a PSX-lit tiled floor material (reuse of
  the existing floor texture is fine), 1 directional light + 2 point lights,
  fog + ambient set, dither compositor enabled.
- Movement clamped to the room's inner AABB (no collision system).
- Esc: first press releases mouse grab, second press (while ungrabbed) quits.
  Clicking the window re-grabs.

## Error handling

- `Engine::init` failure → error log + `false`; main returns nonzero.
- Missing asset / unknown material / bad config → fatal log with the offending
  path or name; startup aborts. No silent fallbacks.
- No exceptions across the public API boundary.

## Verification

- `PSX_SCREENSHOT=<path>` env hook moves into the engine: render 90 frames,
  save PNG, exit. Works for both `game` and `psx-demo` targets.
- Acceptance: (1) `make run-demo` shows the ported demo visually matching the
  pre-refactor screenshot; (2) `make run-game` allows FPS walking in the test
  room with dither active; (3) neither target's sources include Ogre or SDL
  headers.
- No unit-test framework in the scaffold. Config and Input parsing are kept
  free of SDL/Ogre state so tests can be added later.

## Build

- CMake ≥ 3.16, C++17. Targets: `eng` (static), `game`, `psx-demo`.
- Dependencies: system OGRE 14.x (GL3Plus, ParticleFX, STBI plugins), SDL2,
  GLM (system package), toml++ (vendored header).
- Makefile wrapper kept: `make run-game` (default), `make run-demo`.
