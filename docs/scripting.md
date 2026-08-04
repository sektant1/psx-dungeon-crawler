# Lua scripting

How a `.lua` file becomes behaviour on an entity, what it can reach, and what
happens when it breaks. For the object model underneath it see
[ecs.md](ecs.md); for the layering see [ARCHITECTURE.md](../ARCHITECTURE.md).

The runtime is PUC-Rio Lua 5.4 bound with sol2, in `eng_script` — its own static
library at the framework layer, so *who depends on the VM* is a link fact rather
than a habit.

## The object model

A script file is a chunk that **returns a table**. That table is the *class*:

```lua
local Door = {}

function Door:start() ... end
function Door:update(dt) ... end

return Door
```

The chunk runs **once per path**, no matter how many entities carry it. Each
attached entity gets an *instance*: an empty table whose `__index` is the class.

```
class table  (one, shared)      instance  (one per entity)
  update ────────────────────────  __index
  toggle                           entity   ← this entity's handle
                                   props    ← this entity's authored values
                                   …        ← whatever the script sets on self
```

So methods are shared and state is not. Attaching one script to two hundred
entities costs two hundred small tables and one chunk. It is also what makes
hot reload cheap: swapping `__index` changes the code and leaves the state.

**Never store per-entity state on the class.** `Door.open = false` at file scope
is shared by every door in the level; `self.open = false` in `start()` is not.

## Lifecycle

| Callback | When |
|---|---|
| `start()` | once, on the first tick after the script is attached |
| `update(dt)` | every frame, after `start` |
| `fixed_update(dt)` | immediately before each physics step |
| `on_collision(other, hit)` | a solid collider touched something |
| `on_trigger(other)` | a **sensor** collider was entered |
| `on_event(name, data)` | something sent or broadcast to this entity |
| `on_reload()` | this file was hot-reloaded |
| `on_destroy()` | the entity is going away; it is still readable |

Every one is optional. A script defines only what it needs.

### Order within a frame

```
onInput            render-rate look and UI          — nothing scripted
fixed_update(dt)   ─┐
Physics::update     │ immediately before the step it influences
on_collision        │ right after it, so a script reacts in the same frame
on_trigger         ─┘
update(dt)         with the rest of presentation
tickComponentSystems   spin, light animation, lifetimes
World::sync        pushes everything written this frame at the renderer
```

`fixed_update` is defined as **"immediately before a physics step"**, not "on
the fixed clock". `MapPlay` has no fixed loop — it steps physics from
`onPresent` — and the contract still holds there, because whoever steps physics
calls `fixedTick` first.

`start()` is deferred to the first tick rather than firing at attach, so it sees
a fully built level. It runs for every new instance before *any* `update`, so a
script's `start` is never looking at a world another script has already moved.

### Spawning and destroying

`world.spawn` is immediate — you get the handle back and can position it in the
same call. `world.destroy` is **queued** and flushed after the dispatch loop,
because a script calling it from `update` is inside the loop whose views it
would otherwise invalidate. The entity stays `valid` for the rest of that frame.

## Props

Props are per-instance values authored in the scene, reachable as `self.props`.
They are what make one script reusable across many entities instead of one
script per door.

| Type | Authored as | Arrives as |
|---|---|---|
| bool | `true` | boolean |
| number | `2.5` | number (f32; see Limits) |
| string | `"north"` | string |
| vec3 | `[0, 0.5, 0]` | `vec3` |
| entity | `{ "entity": "lever_a" }` | an entity handle, resolved at `start()` |

An `entity` prop that names nothing arrives as `nil` and logs a warning. That is
a warning and not an error because a level may legitimately ship with the
collaborator cut — and the cooker already flags the name at build time, which is
where you actually want to hear about a typo.

Read props in `start()` and keep what you need on `self`. Applying a default
needs care with booleans:

```lua
self.speed = self.props.speed or 4      -- fine: 0 is unusual for a speed
self.once = self.props.once             -- `or true` would flip an authored false
if self.once == nil then self.once = true end
```

## API

### `world`

| | |
|---|---|
| `world.spawn(name) -> Entity` | a new entity, immediately |
| `world.find(name) -> Entity\|nil` | by `Name` |
| `world.destroy(e)` | queued to the end of the tick |
| `world.destroy_hierarchy(e)` | …and everything under it |

`world.find` is a **linear scan** of the `Name` view. Resolve once in `start()`
and keep the handle on `self`; an `entity` prop has already done exactly that.

### Entity

| | |
|---|---|
| `e.valid` | false once destroyed — always safe to read |
| `e.name` | its `Name` |
| `e.position` / `e.rotation` / `e.scale` | the **local** transform |
| `e.world_position` | the composed pose, read-only |
| `e:set_parent(other)` | reparent, keeping the local transform |
| `e:send(name, data)` | deliver `on_event` to that entity |
| `e:script(path) -> table\|nil` | another entity's instance, to call methods on |

`rotation` is Euler **degrees**. Writing it sets that orientation exactly.
Reading it back is lossy, and deliberately: Euler triples are not unique, so
120° of yaw reads back as `(180, 60, 180)` — the same rotation spelled
differently. A script that accumulates by reading its own rotation each frame
will drift; one that keeps its angle on `self` will not. (Unity's `eulerAngles`
carries the same caveat.)

Writes route through `World::setLocalTransform`, which marks the subtree dirty.
`world_position` refuses assignment because `WorldTransform` is derived — a
silent write would be overwritten by the next resolve.

### Components

Any component in the registry, with no binding code:

```lua
if e:has("Spin") then
  e:get("Spin").degrees_per_second = 45
end
e:set("Spin", { degrees_per_second = 180, axis = vec3(1, 0, 0) })
e:add("Visibility")
e:remove("Spin")
```

Field names accept either spelling: `degrees_per_second` and
`degreesPerSecond` find the same field.

Two deliberate holes:

- **`Transform` is not reachable this way.** Use `e.position` / `e.rotation` /
  `e.scale`, which mark the subtree dirty. Reaching the fields directly would
  bypass that and draw at the old pose.
- **Quaternion fields are not exposed.** Same reason `rotation` is degrees.

A misspelled component name raises an error rather than doing nothing — silently
doing nothing is how a typo becomes an afternoon.

### `input`, `physics`, `event`, `log`, `vec3`

```lua
input.down("fire")        input.pressed("interact")   input.mouse_delta()

local hit = physics.raycast(from, dir, 20.0)
-- hit.entity may be nil: the level's batched geometry is one body per region
-- and not an entity, so nil means "the world" rather than "a thing".

event.send(e, "open", { amount = 7 })   event.broadcast("alarm")
log.info("…")  log.warn("…")  log.error("…")

vec3(x, y, z)   -- + - * /, unary -, ==, tostring
                -- :length() :normalized() :dot(o) :cross(o)
```

`input` speaks **action names** from the TOML bindings, never physical keys, so
rebinding never touches a line of Lua.

## Errors

Every callback runs protected with `debug.traceback` installed, so a failure
carries the whole Lua call stack:

```
[error] Script: scripts/door.lua on entity 'iron_door' #42 in update():
  scripts/door.lua:12: attempt to perform arithmetic on a nil value (field 'speed')
stack traceback:
  scripts/door.lua:12: in method 'update'
```

Then that **instance is quarantined** — skipped until revived. Per instance, not
per file: one broken door does not stop the others, and the same traceback
cannot flood the console sixty times a second.

Revive by fixing the file (a successful reload revives it, which is what an
author expects) or with `script.revive`.

## Hot reload

With `ScriptConfig::hotReload`, the script root is polled each frame and changed
files are reloaded. On reload:

- the class table is swapped, so new code takes effect;
- `self` survives — everything the script stored is still there;
- **`start()` does not re-run**; re-running it would wipe the state you are
  iterating on;
- `on_reload()` fires if defined, for anything that must be re-derived;
- quarantined instances are revived.

A file that fails to parse is reported and the previous version stays live, so a
half-typed save cannot kill a running level.

## Console

| | |
|---|---|
| `lua <expr>` | evaluate in the live state (`lua 1+1`, `lua x = 5`) |
| `script.list` | every live instance, and whether it is quarantined |
| `script.reload [path]` | reload one script, or all of them |
| `script.revive` | un-quarantine everything |

---

## HOW TO ADD A SCRIPT

### 1. Write it

`assets/scripts/pulse.lua`:

```lua
-- Pulses an entity's scale.
--
-- Props:
--   speed   number  pulses per second (default 1)
--   amount  number  how far it swells, 0..1 (default 0.2)
local Pulse = {}

function Pulse:start()
  self.speed = self.props.speed or 1
  self.amount = self.props.amount or 0.2
  self.base = self.entity.scale
  self.t = 0
end

function Pulse:update(dt)
  self.t = self.t + dt * self.speed
  self.entity.scale = self.base * (1 + math.sin(self.t) * self.amount)
end

return Pulse
```

### 2. Attach it

In the editor: select the entity, **Add Component → Scripts**, type the path,
add props. Or by hand in the `.scn`:

```json
{
  "id": "shrine_orb",
  "name": "Shrine Orb",
  "scripts": [
    { "path": "scripts/pulse.lua", "props": { "speed": 2.0, "amount": 0.15 } }
  ]
}
```

Several scripts on one entity run in the order listed, which the inspector can
reorder.

### 3. Cook

```sh
make cook SCENE=assets/scenes/my_level.scn
```

A path that does not resolve, or a file that does not parse, **fails here** —
with the Lua error and its line number. `make cook VALIDATE=1` checks without
writing. `python3 tools/assetlint.py` catches dangling script paths across every
scene at once.

### 4. Play

```sh
make scene SCENE=assets/scenes/my_level.scn
```

Edit the `.lua` while it runs and save: the behaviour changes in place, and the
entity keeps its state.

---

## Limits

- **No `io`, `os` or `package`.** Only `base`, `math`, `string`, `table` and
  `debug` are opened. A level script has no business reading the filesystem or
  loading native modules.
- **Numbers authored as props are f32.** `ByteWriter`'s vocabulary is f32 and so
  is every other component's; a Lua number narrows on the way in. Values
  computed *inside* a script are ordinary Lua doubles.
- **No coroutines or scheduler.** Keep timers on `self` and count down in
  `update`.
- **`world.find` is a linear scan.** Cache handles in `start()`.
- **Euler read-back is lossy.** See `rotation`, above.

## Where the code lives

| | |
|---|---|
| `eng/ecs/components/Scripts.h` | the authored component: paths, props, enabled |
| `eng/script/ScriptState.h` | live instance slots, written by the host only |
| `eng/script/ScriptHost.h` | the public surface: ticks, reload, console |
| `engine/src/script/ScriptChunkCache.*` | path → class table, load and reload |
| `engine/src/script/ScriptInstance.*` | the instance pool |
| `engine/src/script/ScriptError.*` | tracebacks, reporting, quarantine |
| `engine/src/script/ScriptContactBridge.*` | physics contacts → entities |
| `engine/src/script/bind/*` | the bindings, one file per module |
