# Projects

How a game that is not *this* game gets made with this engine: a directory, a
`project.toml`, scenes authored in the editor, behaviour written in Lua, and a
runtime that plays all of it without a line of C++.

For the object model behind the scripts see [scripting.md](scripting.md); for
the layering the runtime sits in see [../ARCHITECTURE.md](../ARCHITECTURE.md);
for the scene format see [scenes.md](scenes.md).

## What a project is

A directory. Nothing outside it knows it exists, there is no registry of
installed projects, and copying the directory copies the game.

```
my-game/
  project.toml         identity + configuration (window, render, bindings)
  assets.toml          content manifest, same schema as assets/assets.toml
  scenes/main.scn      authored scenes
  scripts/cube.lua     behaviour
  materials/ textures/ meshes/
  .raven/              per-user working state: cooked maps. Not for committing.
```

`project.toml` is both the identity and the configuration, because the engine
reads `[window]` and `[bindings]` straight out of it:

```toml
[project]
name = "My Game"
main_scene = "scenes/main.scn"   # required; everything else has a default

[window]
title = "My Game"
width = 1280
height = 720

[render]
preset = "psx"                   # RAVEN_RENDER_PRESET overrides it

[scripts]
root = "scripts"                 # resolved from, and watched for hot reload

[bindings]
move_forward = "W"
...
```

**The locomotion bindings are not optional.** `eng::FpsController` reads
`move_forward`, `move_back`, `move_left`, `move_right`, `jump`, `sprint`,
`walk`, `crouch` and `slide` by name and treats an unbound one as fatal, so a
project missing any of them starts and then dies on the first frame that asks.
`createProject` writes all nine.

## Running one

```sh
raven_player <project-dir>     # play a project
raven_player <scene.map>       # play one cooked scene, no project
```

The second form costs nothing: a cooked map is what a project's scene compiles
to, so "play this file" is the same code path with the project fields empty.

| Variable | Effect |
|---|---|
| `RAVEN_PLAY_MAP` | play this cooked map instead of the project's main scene |
| `RAVEN_PLAY_FROM` | `x,y,z` to start at instead of the spawn |
| `RAVEN_RENDER_PRESET` | override the project's render preset |

The editor sets the first two on F5, which is how "play what is on screen"
works for a project whose argument is a directory.

## In the editor

**Project → New project… / Open project… / Open recent project**. With a
project open the editor's content root *is* the project, so a new scene saves
into it, `validate()` resolves meshes and scripts against it, and a cook lands
in `.raven/cooked/` rather than beside the source. F5 launches `raven_player`
on the project instead of `game`.

With no project open the editor behaves exactly as it always has, authoring the
content tree it ships beside. That is not a legacy path — it is how this game
is made.

## Architecture

```
raven_player  (executable: an argument parse and a ProjectApp)
    |
    +-- eng_runtime
    |     Project        project.toml -> a struct. No renderer, no window,
    |                    so the editor and headless tools read it too.
    |     SceneRuntime   cooked .map -> a World. Engine components only.
    |     ProjectApp     the arrangement: world, physics, renderer backend,
    |                    script host, controller. An eng::FpsGameApp.
    |
    +-- eng   (and nothing under game/)
```

`raven_player` links **no game code at all**, and that is enforced rather than
asserted: the `player_purity` ctest reads the built binary's symbol table and
fails on any `game::` or `mapio::` symbol. A claim like that rots the first time
somebody adds a convenient include and a convenient link line.

The dungeon crawler boots through the *same* `ProjectApp`: `game::MapPlayApp`
derives from it and adds what makes it that game — its component table, its
collision layers, its particle collider, its exit portals, its spawn marker.
One scene-boot path in the tree, so the game is the runtime's regression test.

### The component table is a parameter

Every layer that reads a scene takes a `const eng::ecs::ComponentRegistry&`
rather than knowing one. The game passes its own table (engine components plus
`Exit`, `EnemySpawn`, `Pickup`, `ViewmodelRig`); the player passes
`eng::ecs::engineRegistry()`. Components with a stable id the table does not
know are **skipped, not refused**, which is what lets the player open a scene
authored against a richer vocabulary and get everything it does understand.

That is also the current limit: a project cannot define its own components yet.

### Mounting

A project cannot render alone — it has no shaders, compositors, fonts or
default materials. So `eng::assets::mountProject` overlays the project's packs
*over* the engine's own, at higher priority. First hit wins is already the
resolution rule, so a file a project ships shadows the builtin of the same
logical path: overriding a default material is a matter of putting a file in
the right place.

**Today the "engine builtins" are this repository's content pack.** A new
project links no game code and mounts game art. Carving genuine builtins out of
`assets/` is deferred, and is a prerequisite for export.

## Saying where the player starts

An **active `first_person` rig** is a spawn. It states how the player moves
and, by its transform, where they stand:

```json
{
  "id": "player",
  "transform": { "position": [0.0, 1.7, 4.0] },
  "first_person": { "moveSpeed": 6.0, "baseFovDegrees": 70.0 }
}
```

`eng::runtime::SceneRuntime::playerSpawn` reads it at runtime, and the scene
contract and validator count it as filling the Spawn role — so the editor's
answer and the player's are the same answer. A *parked* rig (`active = false`)
counts for neither: kept, not used.

This matters because `PlayerSpawn` is one of **this game's** markers. Before
the rig counted, a scene authored in a project could not say where the player
starts however it was authored, and every new project's first cook was a
refusal.

A scene that authors a `camera` instead is a *shot*: it plays itself through
that camera and the player controller stands down.

## Making things solid

Give geometry a `collider`:

```json
"collider": { "shape": "box", "half_extents": [12.0, 0.1, 12.0] }
```

`eng::ecs::Collider` is an **engine** component (stable id 10), so a project's
collider deserialises in a runtime that has never heard of this game.
`eng::ecs::PhysicsSync` turns one into a static body; a `RigidBody` beside it is
what hands the transform to the simulation.

Worth knowing because the failure is so unhelpful: a floor with no collider
means the player falls through the world, and the screen goes **black** with no
error anywhere. It looks exactly like a broken renderer.

## Adding a scene

1. Author it in the editor with a project open, or write the `.scn` by hand.
2. Give it an active `first_person` rig (or a camera, for a shot).
3. Give the floor a `collider`.
4. Attach scripts with a `scripts` list; `props` is a key→value object.
5. F5, or `scene_cook <scene.scn> --assets <project-dir> --out <map>` then
   `raven_player <project-dir>`.

## What M1 does not do yet

Named so nobody looks for them:

- **Project-defined components.** The player registers the engine set only, and
  the editor's add-component menu still offers this game's markers.
- **Prefabs / scene instancing.** A scene cannot yet be a node inside another.
- **In-editor script editing.** Scripts are files the editor references by path.
- **Export.** Running a project means running `raven_player` against a
  directory; there is no distributable yet.
- **Lua breadth.** World, entity, reflected components, input, math and physics
  are bound. Audio, UI, scene change, spawn/destroy, timers and camera are not.
