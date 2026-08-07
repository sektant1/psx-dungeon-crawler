# Project runtime — M1 of the Godot-like authoring track

**Branch:** `feat/godot-project-runtime`
**Status:** design approved, not implemented
**Scope:** milestone 1 of five (roadmap at the end)

## The claim this milestone makes true

Open the editor, make a new project, place a cube, attach a Lua script that
reads WASD, press F5 — and the thing that runs is `raven_player`, a binary with
**no `game/` objects linked into it at all**.

Today "make a game with this engine" means "write C++ in `game/`". `game` is a
dungeon crawler: `DungeonGen`, `CombatSystem`, `LiveLevel`, an enemy library, an
RPG layer. F5 launches *that*. Everything else on the Godot-parity roadmap —
richer Lua bindings, prefabs, an in-editor script editor, export — assumes a
runtime that boots a project rather than a game, so that runtime is milestone
one and nothing else starts before it.

## What already exists (and therefore is not in scope)

The investigation that produced this design found more standing than expected.
Recorded here so the implementation does not rebuild any of it:

| Thing | Where | State |
|---|---|---|
| Lua 5.4 + sol2, class-table object model, hot reload | `eng_script`, `docs/scripting.md` | complete |
| Script lifecycle: `start`/`update`/`fixed_update`/`on_collision`/`on_trigger`/`on_event`/`on_reload`/`on_destroy` | `eng::script::ScriptHost` | complete |
| Authored per-instance script fields | `eng::ecs::Scripts`, `eng::ecs::Properties` | complete |
| Bindings for World, Entity, generic reflected components, Input, Math, Physics | `engine/src/script/bind/` | complete for M1 |
| Content root discovery + pack manifest + named mount sets | `eng::assets`, `assets/assets.toml` | complete |
| Input map as action → SDL key list | `eng::Config::bindings()` | complete |
| The FPS genre base class (physics setup, console, collider overlay, frame HUD) | `eng::FpsGameApp` | complete, already generic |
| Engine component table | `eng::ecs::registerEngineComponents()` | complete |
| Binary `.map` read/write, fully registry-parameterised | `mapio::readMap`/`writeMap` | complete, in the wrong library |
| A standalone cooked-map player | `game/src/MapPlay.cpp` | exists, game-coupled |
| Editor F5: save → cook → launch child process with env options | `EditorApp::playtest` | exists, hardcoded to `game` |

So M1 is not "write a runtime". It is **relocation and project-ification**: take
the generic half of what `game/` already does, move it to where a project can
use it, and give it a project to read.

## Decisions taken

Recorded with reasons, because each was a real fork.

**D1 — The editor stays on ImGui.** The folder named
`docs/editor_impl_reference_manual/editor_template_to_start_from/` contains one
thing: a clone of upstream Immediate-Mode-UI/Nuklear at `0dbc52f`, with its
working tree half-deleted (`nuklear.h`, `src/`, `README.md`, `LICENSE`,
`Makefile` are removed; `git restore .` inside it recovers them). It is a GUI
library, not an editor template. Rebuilding 42,843 lines of editor on it would
cost a Nuklear→Vulkan-RHI backend, a hand-written docking system, hand-written
gizmos, a hand-written node editor, and a rewrite of ~50 ImGui-driven editor
tests — to arrive back where the editor already is. The intact copy at
`third_party/Nuklear/` is referenced by zero CMake files and stays that way.

**D2 — A project-agnostic player, not a generalised `game`.** `raven_player` is
a new executable. The alternative — teaching `game` a project mode — leaves the
dungeon crawler linked into every project anyone ever makes.

**D3 — Extract the runtime, do not write a second one.** `MapPlay.cpp` is
already "load a cooked `.map`, resolve meshes, build render + physics, run the
loop". A fresh minimal player would be a second scene-boot path, and the two
would drift on exactly the things that are easy to forget: portals, particle
collision, camera selection, primitive mesh caching. The game keeps booting
through the extracted code, so the game is the runtime's regression test.

**D4 — `SceneFactory` does not move.** It builds portal props, treasure
shrines, crystal rings and braziers against named game materials
(`Game/Vfx/PortalDown`, `Game/Kit/Dungeon`). That is content, not runtime.

**D5 — The player's component vocabulary is the engine set.** `raven_player`
registers `eng::ecs::registerEngineComponents()` and nothing else. A `.scn`
carrying game markers (`Exit`, `EnemySpawn`, `Pickup`) still loads — `readMap`
skips unknown stable type ids by contract — those entities simply arrive
without them. Project-defined components are M2+.

**D6 — Two mounts, engine builtins first.** A new project has no shaders,
compositors, fonts or default materials, so it cannot render alone. The player
mounts the engine builtin content and overlays the project's own pack at higher
priority. For M1 the "builtin content" is this repo's existing `content` pack.

## The known limits of M1, stated up front

- **A new project inherits this repo's assets.** Carving genuine engine builtins
  (shaders, compositors, fonts, default materials, primitives) out of `assets/`
  away from dungeon-crawler content is real work and is deliberately deferred.
  M1 projects link no game code and mount game art.
- **The editor's add-component menu still offers game markers.** It reads
  `mapio::coreRegistry()`. Making the menu follow the open project's component
  set is M2.
- **No export.** Running a project means running `raven_player` against a
  project directory. Producing a distributable is M5.
- **No in-editor script editor.** Scripts are files; the editor references them
  by path. M4.

## Architecture

### Target graph

Three changes. Everything else in the build is untouched.

```
                    scene_content  (new: renderer-free, no game::)
                          │
        ┌─────────────────┼──────────────────┐
        │                 │                  │
   game_content      eng_runtime  (new)   scene_cook
   (game markers)         │
        │                 │
      game           raven_player  (new)      scene_editor
```

**`scene_content`** — split out of today's `game_content`, which its own header
comment already calls "the temporarily named authoring/runtime scene bridge".
It takes everything with no `game::` dependency:

```
game/src/scene/MapSerializer.cpp        # already takes const ComponentRegistry&
editor/src/content/SceneDocument.cpp
editor/src/content/SceneSource.cpp
editor/src/content/SceneWriter.cpp
editor/src/content/SceneCook.cpp
editor/src/content/SceneValidate.cpp
editor/src/content/SceneContract.cpp
editor/src/content/SceneRepair.cpp
editor/src/content/GridMath.cpp
editor/src/content/KitCatalog.cpp
editor/src/content/RoomBuilder.cpp
editor/src/content/SceneTemplates.cpp
```

`MapSerializer.cpp`'s only tie to the game is `#include "ComponentRegistry.h"`
(the `mapio` one) for a type it already receives as a parameter; that becomes
`#include <eng/ecs/ComponentRegistry.h>`. `game_content` becomes
`scene_content` plus `game/src/scene/ComponentRegistry.cpp`,
`game/src/audio/ActorSounds.cpp` and `game/src/scene/LayoutToScene.cpp`. Its 30
`coreRegistry()` call sites do not move and do not change.

**`eng_runtime`** — new static library at the `eng` layer (it links `eng`, so
`tools/check_layering.py` is satisfied by construction). Three units:

- `eng::runtime::Project` — reads and validates `project.toml`, resolves the
  main scene, exposes window/render/binding settings. Pure data; no renderer,
  so it is testable headless.
- `eng::runtime::SceneRuntime` — the generic half of `game::MapRuntime`:
  construct over a `World` and a group id, `load()` a cooked `.map`,
  `resolveMeshes()`, `resolvePrimitives()`, `buildAll()`, and report the
  authored rig (`FirstPersonController`, `ThirdPersonCamera`, `ScreenCamera`)
  and player spawn. Everything it touches is an engine component.
- `eng::runtime::ProjectApp : eng::FpsGameApp` — the generic half of
  `MapPlayApp`: build `AppConfig` and `FpsGameConfig` from the project, mount
  content, load the main scene through `SceneRuntime`, construct
  `ScriptHost` with the world, `ScriptConfig{root="scripts", hotReload=true}`
  and the engine registry, `bindInput`/`bindPhysics` it, and choose the camera
  the way `mapHasCamera` already does — an authored camera means play through
  it, no camera means the walk loop.

**`raven_player`** — `main.cpp` plus `eng_runtime` and `scene_content`. Argument
is a project directory (or a `project.toml` path). The existing environment
switches the editor already sets for playtests keep working, because
`ProjectApp` inherits `FpsGameApp`, which is where they are read.

### What stays behind in `game/`

`game::MapRuntime` keeps `enemySpawnPlacements()`, `pickupPlacements()`,
`npcPlacements()`, `placements()`, `levelExit()`, `exitYawDegrees()`, the
`ViewmodelRig` half of `playerRig()` and `ActorSoundSet` carriage, implemented
over a contained `SceneRuntime`. `game::MapPlayApp` derives from `ProjectApp`
and adds `buildExitPortals()` and the viewmodel. `game`'s behaviour must not
change; `MapPlay`'s existing tests are the check.

### The project format

```
my-game/
  project.toml
  assets.toml              # pack manifest, same schema as assets/assets.toml
  scenes/main.scn
  scripts/player.lua
  meshes/  textures/  materials/
  .raven/                  # per-user, gitignored: cooked maps, recents, autosave
```

```toml
[project]
name = "My Game"
main_scene = "scenes/main.scn"

[window]
title = "My Game"
width = 1280
height = 720

[render]
preset = "psx"

[bindings]
move_forward = ["W", "Up"]
move_back    = ["S", "Down"]
move_left    = ["A"]
move_right   = ["D"]
jump         = ["Space"]
quit         = ["Escape"]
```

`[bindings]` is not a new mechanism: `eng::Config` already parses that table as
action → list of SDL key names, and `FpsGameConfig` already names its actions as
strings. A project therefore gets a remappable input map for free.

Reading it uses `eng::Config`, so `project.toml` flattens to dotted keys and the
loader asks for `project.main_scene` and friends. A missing `[project]
main_scene` is a hard error with a named file; everything else has a default.

### Mounting

`eng::assets::init()` discovers exactly one root and mounts named sets from its
manifest. A project needs its own content *and* the engine's, so `eng::assets`
gains one function:

```cpp
// Append a project's pack at the highest priority, over whatever is mounted.
// The project directory must contain assets.toml. Fails and mounts nothing on
// a missing or malformed manifest, leaving the builtin mounts intact.
bool mountProject(const std::filesystem::path& projectDir);
```

`raven_player` calls `init()` (builtins, found the usual way) then
`mount("game")` then `mountProject(dir)`. First hit wins is already the
resolution rule, so a project file shadows a builtin of the same logical path —
which is how a project overrides a default material without a data edit
anywhere else.

### Editor changes

Smallest set that makes the workflow real:

- `EditorSettings` grows a recent-projects list beside the existing recent
  scenes; `mState.assetRoot` becomes the open project's root.
- **File → New Project…** writes `project.toml`, `assets.toml`, `scenes/`,
  `scripts/` and a `main.scn` containing a camera and a floor, then opens it.
- **File → Open Project…**, **File → Recent Projects**.
- `EditorApp::playtest` launches `siblingExecutable(mExecutablePath, "raven_player")`
  with the project directory when a project is open, and `game` otherwise — the
  dungeon crawler's own F5 keeps working unchanged.

The script side needs nothing new in M1: `eng::ecs::Scripts` is already an
inspectable component with authored per-instance fields, and the existing
inspector already edits it.

## Testing

Unit and headless, in the style the repo already uses:

| Test | Asserts |
|---|---|
| `project_tests` | `project.toml` parse: defaults, missing `main_scene` is an error, bindings round-trip, path resolution relative to the project root |
| `scene_runtime_tests` | cooked `.map` → `World`: entity count, hierarchy, primitive cache reuse, authored rig detection, unknown component ids skipped |
| `runtime_script_tests` | boot a project headless, tick N frames with a script that translates its entity, assert the transform moved and `start()` ran once |
| `player_purity_test` | `nm`/`readelf` over `raven_player` finds **no** `game::` symbol |
| existing `map_play`/game tests | unchanged behaviour after the extraction |

`player_purity_test` is the one that matters in six months. "Zero game C++" is a
claim that rots silently the first time somebody adds a convenient include; the
repo already enforces its layering with a lint (`tools/check_layering.py`) rather
than a convention, and this is the same idea one level up.

On-screen verification is not optional here (CLAUDE.md: *verify on screen, not
just in the compiler*). The slice ends with a screenshot of `raven_player`
running the generated project, read back and checked.

## The slice, exactly

1. `make editor`
2. File → New Project… → `/tmp/my-game`
3. The generated `main.scn` opens: camera, floor, one cube.
4. Select the cube, add a `Scripts` component, point it at `scripts/player.lua`.
5. `scripts/player.lua` reads the movement actions in `update(dt)` and
   translates its entity.
6. F5.
7. `raven_player /tmp/my-game` opens, and the cube moves under WASD.
8. `nm build/raven_player | grep game::` prints nothing.

## Roadmap beyond M1

Each is its own spec, in this order:

- **M2 — Lua API breadth.** Audio, HUD/UI drawing, scene change, spawn/destroy,
  timers, camera control, save/load. Project-defined components, which also
  fixes the editor's add-component menu (D5, and the second known limit).
- **M3 — Prefabs / scene instancing.** A scene usable as a node inside another
  scene, in both the editor and the runtime. This is Godot's central idea and
  the largest single piece.
- **M4 — Script authoring UX.** Create/attach a script from the inspector,
  runtime error console in the editor, hot reload driven from the editor.
- **M5 — Export.** Project + cooked pack + player binary → a distributable.

Carving engine builtins out of `assets/` is a prerequisite for M5 and can land
any time after M1.
