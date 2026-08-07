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

## Scene instancing

The central idea, and Godot's: a torch, a pillar, an enemy is authored **once**
as its own `.scn` and placed anywhere. Fixing it once fixes every placement.

```json
{
  "id": "torch_a",
  "name": "Torch A",
  "transform": { "position": [2.0, 0.0, 0.0] },
  "instance": { "scene": "scenes/torch.scn" }
}
```

Not called a prefab, though that is the usual word: `prefab` already means a
kit piece in this format, and one word for two things in one struct is how a
file format acquires a bug nobody can describe.

**What expansion does.** Before the cooker, the validator or the editor's
preview sees the document, every `instance` is replaced by the contents of the
scene it names:

- The placement **survives**, stripped of its `instance`. It is the node the
  contents hang from, so moving it moves the whole torch, and a script on it is
  the torch's script. It keeps its name, layer and transform.
- Ids are namespaced with the placement's — `torch_a/flame` — so two placements
  of one scene cannot collide and a diagnostic names something findable.
- An inner root is parented to the placement; an inner child keeps its own
  parent, namespaced to match. No transform arithmetic happens: the cooker
  already resolves transforms through the parent chain.
- Contents inherit the placement's layer, so hiding the layer a torch is on
  hides its flame.
- Nesting works, to `kMaxInstanceDepth` (8).

Refused, with the document left untouched: a scene that does not resolve, one
that will not parse, and cycles — reported with the chain of files in them,
because "cycle detected" alone is something you have to reproduce before you
can act on it.

Per-instance overrides ("this torch is blue") deliberately do **not** exist yet.
An override system is a merge policy, and one invented alongside its first use
gets redesigned the moment somebody nests two deep. Make a variant scene.

### Component scenes

A scene meant to be placed rather than played declares it:

```json
{ "format": "raven-scene", "version": 2, "id": "scene.torch",
  "component": true,
  "entities": [ ... ] }
```

The scene contract's rules are about *levels*: a level with nothing to look
through and nowhere to start is broken and has to say so. A pillar has neither
and never will. Without this flag every prop in a project would need a camera
nobody looks through — or could not be cooked at all, and so could never be
spawned at runtime. Everything else still applies: a prefab that does not
resolve, or a script that will not compile, is wrong in a pillar exactly as it
is in a level.

### At runtime

The same `.scn` an author places is what a script spawns:

```lua
local handle = game.spawn_scene("scenes/torch.scn", vec3(4, 0, -2))
game.despawn(handle)
```

The handle is a lifetime group, so despawning is one call and cannot take
anything else with it — spawned objects get groups distinct from the level's,
so changing scene does not destroy a script's effects and vice versa. `0` means
it did not load, which a script can test.

## Shipping a build

```sh
raven_export <project-dir> --out <dir> [--name <exe>] [--overwrite]
```

or **Project → Export…** in the editor, which writes to `<project>-build`
beside the project and calls the same function — so a build made in the editor
and one made on a build machine are the same build.

The result runs on its own:

```
my-game-build/
  my-game            the player, named from the project, stripped
  assets/            engine content + a generated assets.toml
  project/           project.toml, scripts, scenes, and .raven/cooked/*.map
```

Run the executable. No arguments, no install, nothing from the source tree.
That works because of two things that already existed: `eng::assets::init`
discovers `<exe>/assets`, and `raven_player` with no argument now looks for a
`project/` beside itself.

**Every scene is cooked fresh** into the build, including `component: true`
ones — a prop is not playable but it is spawnable, so it needs its `.map`. The
project's own `.raven/` is never copied: it holds whatever the last playtest
left, which may be older than the scenes beside it, and shipping a stale map is
a bug that only appears on somebody else's machine.

### What gets included, and why it is a list

`engineRuntimeDirectories()` names the parts of the engine's content a runtime
cannot start without: shaders, compositors, fonts, ui, materials, textures,
particles, config. About 14 MB.

It is a list rather than "everything" because the content tree this engine
ships beside is **412 MB** — 302 MB of which is `source/`, pipeline inputs that
nothing ever loads, and 92 MB this game's meshes, which somebody else's project
has no use for. Exporting the lot would make every game built with this engine
a 400 MB download of somebody else's art.

This is the honest version of the "carve engine builtins out of `assets/`" job
the runtime has owed since M1. It does not reorganise the tree; it states which
parts of it are the engine's. Splitting it for real would let this list become
"the engine pack", and until then this is where the answer lives.

**The binary is stripped.** This tree builds RelWithDebInfo, so the player
carries ~200 MB of DWARF into a 15 MB program — an unstripped export of a
project with 14 MB of content came to 219 MB, and the symbols are no use to
somebody handed a binary. `--strip-debug`, not `--strip-all`, so a crash in a
shipped game still produces function names. Pass `keepDebugSymbols` to opt out.

### Refusals

The exporter would rather produce nothing than something broken:

- a directory that is not a project;
- an output directory that is not empty, unless `--overwrite` — the directory
  somebody types by mistake usually has something in it;
- a missing player binary;
- **no scene that cooked** — a build with nothing playable in it opens to a
  black window on somebody else's machine. Scenes that fail individually are
  reported by name and the rest still ship.

## Writing scripts in the editor

The editor deliberately does **not** edit script text. A code editor is a large
thing to build badly and everyone already has one they prefer, so the editor's
job is the parts a text editor cannot do.

**New script.** In the Inspector's Scripts block, `new script...` writes a
correctly shaped file into the project's `scripts/`, attaches it to the
selected entity, and opens it. The name comes from the entity — "My Door"
becomes `scripts/my_door.lua` with a `MyDoor` class — and it never overwrites
an existing file. The template is a working script rather than a stub, because
the class-table shape is the one thing you have to know before you can write
anything:

```lua
local MyDoor = {}
function MyDoor:start()  self.speed = self.props.speed or 1.0 end
function MyDoor:update(dt) end
-- function MyDoor:on_trigger(other) end   -- and the rest, commented
return MyDoor
```

**Edit.** Each attached script has an `edit` button. It opens `$VISUAL`, then
`$EDITOR`, then the desktop default — checked in that order because a desktop
handler that opens `.lua` in a web browser is a real configuration people have.
It is spawned detached and never waited on.

**Hot reload** is already on: `raven_player` watches the project's script root,
so saving a `.lua` in your editor swaps the class table under every live
instance while the game runs. Instance state on `self` survives, `start()` is
not re-run, and `on_reload()` fires if the script defines it. A file that will
not compile leaves the running one alone — a half-typed save must not kill a
level.

### Seeing what broke

The **Scripts** tab at the bottom of the editor shows the errors the running
game reported: which script, which entity, which callback, the Lua message and
the traceback, with a button to open the file.

It is beside Problems rather than inside it because the two answer different
questions — Problems is what is wrong with the scene on disk, Scripts is what
went wrong when it ran, and only one of them can be fixed without pressing
play.

The channel is the playtest log. A playtest is a separate process by design, so
there is no in-memory path from the running game back to the editor; its stdout
is already redirected to `artifacts/playtest.log`, and the editor tails that
twice a second while the game is up, then once more when it exits (the error
that killed it is written last). That means the format
`eng::script::reportScriptError` writes is a contract between two processes
with no shared type — which is exactly what `script_workshop`'s tests exist to
keep honest.

## The Lua surface

On top of the object model in [scripting.md](scripting.md) — `world`, `entity`,
reflected components, `input`, `physics`, `event`, `log`, `vec3` — a project
runtime binds these. Everything here is available in `raven_player`; a host
that binds fewer subsystems (a headless test, a combat sim) still loads the
same scripts, and an unavailable call logs and does nothing rather than being
nil.

### Scheduling

```lua
timer.after(1.5, function() game.load_scene("scenes/level2.scn") end)
local id = timer.every(0.25, function() self.hp = self.hp + 1 end)
timer.cancel(id)
```

Timers run on **game time**, so pausing or slowing the clock reaches every one
of them at once — which is the reason they are not left to scripts to count
themselves. A callback may cancel its own timer, or schedule more; one that
errors is reported and dropped without stopping the others. A timer is not
scoped to the entity that created it: the closure keeps its upvalues, and a
handle re-checks validity, so one that outlives its entity is a safe no-op.

### Audio

```lua
sound.play("audio/sfx/hit.wav", { gain_db = -3, pitch = 1.2, bus = "weapons" })
sound.play_at("audio/sfx/step.wav", self.entity.position)
local amb = sound.loop("audio/music/cave.ogg", { bus = "music" })
amb.stop()
sound.bus_volume("music", 0.4)
```

`play_at` implies spatialisation by having said where the sound is. Buses are
named (`master`, `music`, `ambience`, `dialogue`, `weapons`, `sfx`, `ui`,
`warnings`); an unknown name plays on `sfx` and warns, so a typo is audible
rather than silent.

### The runtime

```lua
game.load_scene("scenes/level2.scn")   -- deferred to the next frame
game.quit()
game.time()                            -- game seconds
game.set_time_scale(0.25)              -- slows timers and physics with it
camera.position(); camera.forward()
```

`load_scene` names the scene the way an author does; the runtime knows where
the cooked form lives. It is applied at the top of the next frame, never
inline: the script asking for it is running on an instance the switch is about
to destroy. A scene that will not load leaves the player where they are, with
an error naming the file.

The switch destroys exactly the outgoing scene's entities — they carry a
lifetime group — so anything spawned outside it survives. The camera is
read-only: it belongs to whatever drives it, and a script that could move it
would fight that thing every frame.

### Saving

```lua
save.set("checkpoint", 3); save.set("name", "ilsabet"); save.set("open", true)
local n = save.get("checkpoint", 0)   -- default for missing OR wrong-typed
save.commit()                          -- explicit; writes to disk
save.clear()                           -- new game
```

Numbers, booleans and strings, written as tab-separated plain text inside the
project's `.raven/` — hand-editable, because the first thing anybody does with
a save system is corrupt a save and need to look at it. `commit` writes through
a temporary and renames, so a crash mid-write leaves the previous save intact.

### Spawning things that are visible

`world.spawn` gives a bare entity; add components to make it something:

```lua
local e = world.spawn("crate")
e:add("Transform"); e.position = vec3(0, 1, -4)
e:add("PrimitiveMesh"); e:add("MeshRenderer")
```

The runtime resolves geometry for primitives added after load, once per frame,
so an entity built this way appears on the frame it is created. Changing a
`PrimitiveMesh` afterwards keeps the old geometry — clear `MeshRenderer.mesh`
to ask for it again.

## What is still missing

Named so nobody looks for them:

- **Project-defined components.** The player registers the engine set only, and
  the editor's add-component menu still offers this game's markers. This is the
  largest remaining piece and is its own milestone.
- **Per-instance overrides.** A placed scene cannot be tweaked in place; make
  a variant scene instead.
- **Instanced contents in the outliner.** A placement is one row in the editor;
  its contents appear when cooked, not in the tree.
- **UI from Lua.** No HUD or menu drawing; gameplay must not depend on ImGui,
  so this needs the engine's own UI canvas exposed rather than ImGui bound.
- **In-editor script text editing.** Deliberate: creating, attaching, opening
  and error reporting are covered; typing the code happens in your own editor.
- **Jump to the failing line.** The Scripts panel opens the file, but not at
  the line — the log carries it and nothing parses it out yet.
- **Windows and macOS builds.** Export ships the player it is given, for the
  platform it was built on. There is no cross-compilation and no installer.
- **Trimming unused assets.** The engine content set is a fixed list, not a
  trace of what a project actually references, so a small game still ships
  ~14 MB of engine content.
- **A window icon.** An exported build logs a warning about the engine's own
  icon path; a game should ship its own, and there is no field for one yet.
