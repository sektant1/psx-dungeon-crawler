# Lua scripting — design

Date: 2026-08-03
Status: approved, ready for an implementation plan

Entity and level scripting in Lua 5.4 via sol2, attached as an authored ECS
component, with a Unity/Godot-shaped `start()` / `update(dt)` lifecycle.

---

## 1. Goal and scope

Scripts carry **entity behaviours and level/quest logic**: a door that opens, a
trap that fires, a patrolling prop, a lever that drives a door, a trigger volume
that starts an encounter. C++ keeps owning the player, combat, weapons, enemy AI
and the renderer. This is not a migration target for existing gameplay, and the
design does not pay for one.

### In scope

- A `Scripts` component: several scripts per entity, each with authored
  per-instance properties.
- Lifecycle: `start`, `update(dt)`, `fixed_update(dt)`, `on_destroy`,
  `on_event(name, data)`, `on_collision(other, hit)`, `on_trigger(other)`,
  `on_reload`.
- A binding surface over the ECS World, input, physics raycasts, logging and
  script-to-script messaging.
- Errors reported with a full Lua traceback, then the failing instance
  quarantined.
- Hot reload of `.lua` files in development.
- A `lua` REPL in the existing `DebugConsole`.
- Editor authoring of the component, and cook-time validation of every script
  path and its syntax.

### Out of scope

- Level scripts as a separate concept. A level script is a `Scripts` component
  on an ordinary authored entity (`LevelLogic`); it gets the same lifecycle and
  reaches other entities by name. No second attachment path through the scene
  format, cooker, editor and schema.
- Running scripts in the scene editor's preview world. The editor authors and
  validates; `F5` cooks and playtests, which is the existing workflow. A bad
  script can never wedge the editor.
- Coroutines, a scheduler, script-driven rendering, a Lua debugger protocol,
  and any binding for a subsystem no script in this scope needs.

---

## 2. Layering and build

### Target

A new static library **`eng_script`**, at the *framework* layer but as its own
CMake target. It links `eng_framework`; `eng` links it whole-archive, matching
how the other layers are linked.

Not folded into `eng_framework` because that target is linked by every engine
test and by the editor's preview world. A separate target makes "who depends on
the VM" a link fact, and confines sol2's compile cost to one place. The editor
links it directly, for cook-time validation only.

`tools/check_layering.py` maps files to layers by path, so this is two lines
rather than a new layer:

- `SOURCE_RULES`: `("src/script/", "framework")`
- `HEADER_RULES`: `("script/", "framework")`

The framework layer may include anything below it, so bindings reach `World`,
`Physics` and `Input` legally, and an upward include from `eng_script` remains a
lint failure.

### Dependencies

Both added to `cmake/Dependencies.cmake` — the single source of truth — via CPM,
exactly pinned, fetched and built from source like every other dependency.

- **Lua 5.4.7**, `lua/lua`, `DOWNLOAD_ONLY`, plus an `add_library(lua STATIC …)`
  recipe in `Dependencies.cmake` with an **explicit source list** (not a glob —
  CMake globs do not re-run on a new file), excluding `lua.c` and `luac.c`.
  PUC-Rio ships no CMake; writing the twelve-line recipe is preferable to
  pinning a third party's fork of the build system.
- **sol2 v3.3.0**, `DOWNLOAD_ONLY` → an INTERFACE target over its `include/`.
  The multi-header distribution, not the single header, so a PCH can carry it.
  `SOL_ALL_SAFETIES_ON=1` in Debug builds; off in Release.

PUC-Rio Lua rather than LuaJIT: LuaJIT is 5.1-era, ships no CMake build, and its
throughput only matters for the gameplay-migration scope this design excludes.

sol2 requires exceptions. Nothing in the first-party compile flags disables
them. Jolt's `-fno-rtti` is Jolt's own and does not reach this target.

### Build cost

sol2 is template-heavy and **this repository never clean-builds** (see
`CLAUDE.md`). Mitigations, both required:

- `eng_script` gets its own PCH carrying `<sol/sol.hpp>` and `<entt/entt.hpp>`.
- Bindings are split across several `.cpp` files by module, not concentrated in
  one, so editing one binding does not recompile all of them.

`<sol/sol.hpp>` must never appear in a public `eng/` header. `ScriptHost` is
PIMPL'd for exactly this reason.

---

## 3. Data model

### The component

`engine/include/eng/ecs/components/Scripts.h`:

```cpp
namespace eng::ecs {

// One authored value on a script instance. A tagged union rather than a variant
// so the byte serialiser and the inspector both switch on one enum.
struct ScriptProp {
    enum class Type : uint8_t { Bool, Number, String, Vec3, Entity };
    std::string key;
    Type type = Type::Number;
    bool  b{};
    double n{};
    glm::vec3 v{};
    std::string s;   // String's value; also Entity's target entity name
};

// One script attached to an entity.
struct ScriptRef {
    std::string path;               // logical asset path, "scripts/door.lua"
    std::vector<ScriptProp> props;  // authored, per instance
    bool enabled = true;
};

// Every script on this entity, in author order — which is the order their
// callbacks run in.
struct Scripts {
    std::vector<ScriptRef> items;
};

} // namespace eng::ecs
```

`Scripts` is an **engine** component. Stable type id **33** — the next id above
the highest in use (32, `PrimitiveMesh`). Ids 24, 29, 30 and 31 are gaps in the
engine block and are deliberately not reused, per the registry's rule.

It **cannot** be a reflected POD component: `Field{type, offset}` describes a
fixed layout, and this is a variable-length list of heterogeneous values. So it
hand-writes `serialize`/`deserialize` and its editor I/O, the same category as
`Name`, `Transform`, `MeshRenderer`, `LightRef`, `RenderNode` and
`KinematicControl`, which already do.

`Entity`-typed props hold another entity's **name**, resolved once at `start()`.
Mechanically a string; it exists as its own type so the inspector can offer an
entity picker and the cooker can check the name resolves. `target = "lever_a"`
is the level-logic case this feature is for.

### Runtime state is a separate component

`engine/include/eng/script/ScriptState.h`:

```cpp
namespace eng::script {

// Live instances for this entity's Scripts, owned by ScriptHost. Written by the
// host, never by an author or a caller — the same contract as NodeRef and
// BodyRef. Holds opaque slot handles, not sol types, so this header does not
// pull the VM into anything that includes it.
struct ScriptState {
    std::vector<uint32_t> instances;  // slots in ScriptHost's instance pool
};

} // namespace eng::script
```

Not registered with the `ComponentRegistry`: it is never serialised and never
authored, exactly like `NodeRef` and `BodyRef`.

The split is what makes reload and re-instantiation trivial: `Scripts` is purely
authored data, so rebuilding a script is "drop `ScriptState`, rebuild from
`Scripts`". It also keeps sol out of `eng/ecs/`.

### Scene format

Source `.scn` (TOML), an array of tables per entity:

```toml
[[entity]]
name = "iron_door"

  [[entity.script]]
  path = "scripts/door.lua"
  props = { speed = 2.0, target = "lever_a", starts_open = false }
```

Prop types are inferred on read from the TOML value — boolean, number, string,
a 3-element array as `Vec3` — except `Entity`, which the writer emits as a
tagged form so the type survives a round trip:

```toml
  props = { target = { entity = "lever_a" } }
```

Cooked `.map` payload, written by the component's own serialiser:

```
u16 itemCount
  per item:  string path
             u8     enabled
             u16    propCount
               per prop: string key, u8 type, then the value in its natural
                         encoding (bool / f64 / vec3 / string)
```

Appending trailing fields later follows the registry's append-only rule: a
short payload decodes with defaults.

### Where scripts live

`assets/scripts/`, resolved by explicit path through `eng::assets::resolve`.
**Not** added to `assets.toml`'s `resources` list — that list is Ogre's flat
resource group, and the manifest's own rule is that anything read by explicit
path stays out of it. The manifest's directory-map comment gains a
`scripts/  Lua entity and level scripts` line.

---

## 4. Runtime

### The host

`eng::script::ScriptHost` owns the `sol::state`, the loaded-chunk cache, the
instance pool, and binding registration. Constructed by the application beside
the World, like `RendererSceneBackend` — not an `eng::System`, because its
ordering relative to physics is the substance of the design and a generic
`update(dt)` would hide it.

```cpp
class ScriptHost {
public:
    ScriptHost(ecs::World&, const ScriptConfig&);
    ~ScriptHost();

    // Optional subsystems. A host given none still runs scripts that only
    // touch the World — which is what makes the headless tests real.
    void bindInput(Input&);
    void bindPhysics(Physics&);

    void fixedTick(float dt);   // fixed_update, immediately before a physics step
    void tick(float dt);        // start() for new instances, then update()
    void drainContacts();       // on_collision / on_trigger, after a physics step
    void pollReload();          // dev only; DirectoryWatcher

    bool executeConsole(const std::string& line, std::string& out);
};
```

### Instance model

A `.lua` file is a chunk that **returns a table** — its *class*. Loaded and
cached once per logical path.

```lua
-- assets/scripts/door.lua
local Door = {}

function Door:start()
  self.open   = self.props.starts_open
  self.lever  = self.props.target        -- already resolved to an entity handle
  self.closed_y = self.entity.position.y
end

function Door:update(dt)
  local want = self.open and (self.closed_y + 3.0) or self.closed_y
  local p = self.entity.position
  p.y = p.y + (want - p.y) * math.min(1.0, dt * self.props.speed)
  self.entity.position = p
end

function Door:on_event(name)
  if name == "toggle" then self.open = not self.open end
end

return Door
```

Each attached script gets `setmetatable({}, { __index = class })` as its `self`,
with `self.entity` (the owning entity handle) and `self.props` (a table, with
`Entity` props already resolved to handles) assigned before `start()`. Per-entity
state is just fields on `self`. Reload swaps the class table, so instance state
survives.

An `Entity` prop whose name resolves to nothing arrives as `nil` and logs a
warning naming the script, the entity and the prop key. It is a warning rather
than an error because an entity may legitimately be absent — a door in a level
where its lever was cut — and the cooker already fails the build on a name that
does not exist in the authored scene.

Instances run in **author order** — the order of `Scripts::items`, which is the
order shown and reorderable in the inspector.

### Tick order

Stated once, here, and reproduced in `docs/scripting.md`:

```
onFrameBegin / onInput   render-rate input           — nothing scripted
fixedTick(dt)            → fixed_update(dt)          — immediately before …
Physics::update()
drainContacts()          → on_collision / on_trigger
tick(dt)                 → start() for instances created since the last tick,
                           then update(dt)
tickComponentSystems(dt)
World::sync()
```

`fixed_update` is defined as **"runs immediately before a physics step"**. That
keeps the contract true both in a proper fixed-step app and in `MapPlay`, which
steps physics from `onPresent` with no fixed loop. The host does not decide when
physics runs; whoever steps physics calls `fixedTick` first.

`start()` is deferred to the first `tick()` after the instance is created rather
than firing at attach, so a script's `start` sees a fully built level.

### Structural safety

Three rules, each closing a specific failure:

- The update loop iterates a **snapshot** of the entity list — the same
  technique `EventBus::dispatch` already uses — so a script may spawn an entity
  mid-loop and get its handle back immediately without invalidating the
  iteration.
- **Destroys are queued** and flushed after the loop.
- `on_destroy` fires from an entt `on_destroy<ScriptState>` hook, where the
  entity is still valid. Spawns are refused (and logged) during teardown.

---

## 5. Binding surface

A hybrid: hand-written where it earns it, reflection-driven for the long tail.
The goal is that a component added later is scriptable the moment it is
registered, with no Lua-side work — the property this repository already prizes
in its component table.

### Globals

| Module | API |
|---|---|
| `log` | `log.info/warn/error(msg)` → `eng::log`, category `script` |
| `world` | `world.spawn(name) -> entity`, `world.find(name) -> entity\|nil`, `world.destroy(e)`, `world.destroy_hierarchy(e)` |
| `input` | `input.down(action)`, `input.pressed(action)`, `input.mouse_delta() -> dx, dy` (two numbers; `eng::Input::mouseDelta` is a `vec2` and is not widened to a `vec3` just to fit the usertype) |
| `physics` | `physics.raycast(from, dir, dist, mask) -> {entity, point, normal, fraction}\|nil` |
| `event` | `event.send(entity, name, data)`, `event.broadcast(name, data)`. `event.send(e, …)` and `e:send(…)` are the same call reached two ways; the method form reads better inside a script that already holds the handle. |
| `vec3` | usertype: constructor, `x/y/z`, `+ - *`, `:length()`, `:normalized()`, `:dot(o)`, `:cross(o)` |

`input` maps directly because `eng::Input` is already action-name based
(`isDown("fire")`), so no new taxonomy is introduced.

### The entity handle — hand-written, because it is the hot path

`e.valid`, `e.name`, `e.position`, `e.rotation`, `e.scale`, `e.world_position`
(read-only), `e:set_parent(o)`, `e:send(name, data)`, `e:script(path) -> table`.

`e:script()` returning another entity's instance table is what lets `lever_a`
call `iron_door:toggle()` directly, without routing every interaction through an
event. It returns the **first** instance whose path matches, or `nil` when the
entity carries no such script — never an error, so a script may probe for an
optional collaborator.

Writes to `position`/`rotation`/`scale` route through `World::setLocalTransform`
so the dirty flag propagates to the subtree. `world_position` is read-only
because `WorldTransform` is derived — the ECS docs already state that rule.

### The reflection fallback — for everything else

```lua
e:get("Spin").degrees_per_second = 90
e:set("Spin", { degrees_per_second = 90 })
e:has("Spin");  e:add("Spin");  e:remove("Spin")
```

Driven entirely off `ComponentType::fields` and `ComponentType::instance`, about
150 lines. Field name lookup is a linear scan of the field array — component
field lists are short, and this is not the per-frame hot path.

`Transform` is **excluded** from the generic path; it has the first-class
accessors above, so the dirty-flag invariant cannot be bypassed by writing a
field offset directly.

### Handle invariants

Both rules are stated in `docs/ecs.md` and are honoured explicitly, with a test
each:

- The field proxy stores `{entity, const ComponentType*}` and **re-resolves
  `instance()` on every access**. It never caches a component pointer, because
  any `emplace` can move a pool.
- The Lua entity handle is `{World*, entt::entity}`, validated with
  `registry().valid()` on every use. A handle to a destroyed entity reports
  `valid == false`; it does not crash and does not resurrect a recycled id.

### Cost

`world.find(name)` is a linear scan of the `Name` view. Documented as such, with
the guidance to resolve handles once in `start()` and cache them on `self` —
which `Entity` props already do automatically.

---

## 6. Errors, hot reload, console

### Errors and tracebacks

Every entry point — `start`, `update`, `fixed_update`, `on_destroy`, `on_event`,
`on_collision`, `on_trigger`, `on_reload` — is called as a
`sol::protected_function` with a `debug.traceback` message handler, so a failure
carries the Lua call stack rather than only the top frame.

Chunks are loaded under their **logical asset path** as the chunk name, so
frames read `scripts/door.lua:12:` instead of `[string "..."]:12:`.

One error produces one report, at `Error`, category `script` (so the
`DebugConsole` category filter picks it up):

```
script scripts/door.lua on entity 'iron_door' #42 in update():
  attempt to perform arithmetic on a nil value (field 'speed')
  stack traceback:
    scripts/door.lua:12: in method 'update'
```

The C++ side of the boundary is guarded too: an exception thrown by a binding is
caught and reported the same way rather than unwinding through the Lua VM.

After reporting, the **instance is quarantined** — disabled, so it cannot emit
the same traceback sixty times a second. A hot reload of its file, or
`script.revive`, restores it. Quarantine is per instance, not per script file:
one broken door does not stop the others.

### Hot reload

A `DirectoryWatcher` over `assets/scripts` filtered to `.lua`, polled once per
frame — the pattern the engine already uses for asset hot reload. Gated by a
config flag, on in development, off in release.

On change:

- the chunk re-runs and the class table is swapped;
- instances keep their `self` state and pick up the new methods through
  `__index`;
- `start()` is **not** re-run — re-running it would reset state, which is the
  opposite of what an iteration loop wants;
- `on_reload()` fires if the class defines it, for a script that does want to
  re-derive something;
- quarantined instances are revived.

A file that fails to parse on reload is reported and the previous class stays
live, so a half-typed save does not kill a running level.

### Console

Registered on the existing `DebugConsole`:

| Command | Does |
|---|---|
| `lua <expr>` | evaluates in the host state through the same traceback handler, prints the result |
| `script.list` | every live instance: entity, path, enabled/quarantined |
| `script.reload [path]` | reload one script, or all |
| `script.revive <entity\|all>` | un-quarantine |

---

## 7. Physics contacts

`eng::Physics::setContactCallback` is a **single slot**, already claimed by
`game/src/main.cpp:394`. It becomes multi-subscriber:

```cpp
using ContactToken = uint32_t;
ContactToken addContactCallback(HitCallback);
void removeContactCallback(ContactToken);
```

There are exactly two call sites (`game/src/main.cpp:394`,
`game/tests/PhysicsTests.cpp:252`). Both migrate; the single-slot setter is
deleted rather than left as a wrapper.

`ScriptContactBridge` (in `eng_script`) registers one callback, maps
`BodyHandle → entt::entity` from the `BodyRef` view, and queues
`{self, other, point, normal, impulse}`. `drainContacts()` dispatches after the
physics step.

`Collider::sensor` decides the split: a sensor body dispatches `on_trigger(other)`,
a solid one `on_collision(other, hit)`. A component read, not a second mechanism.

Contacts are already delivered on the main thread — `Physics` collects them on
Jolt's job threads and flushes them itself (`engine/src/physics/Physics.cpp:318`).
The bridge therefore needs no locking; it queues only to keep Lua from running
inside `Physics::update()`.

---

## 8. Editor and cook validation

### Inspector

A bespoke block for `Scripts` in `editor/src/ui/ComponentInspector.cpp`: the
attached list, each row carrying a path field with a picker scoped to
`assets/scripts`, an enabled toggle, reorder and remove, and a props table
(add/remove key, a type dropdown, a per-type value widget). `Entity` props get
an entity picker.

Bespoke because the generic field walker cannot express a variable-length list —
the same reason the component hand-writes its serialiser.

### Scene I/O

Hand-written read and write for the `[[entity.script]]` array of tables in
`editor/src/content/SceneSource.cpp` and `SceneWriter.cpp`, beside the existing
`FieldType` switch, as a component-name special case.
`assets/schemas/scene.schema.json` gains the matching shape.

### Cook-time validation

In `editor/src/content/SceneValidate.cpp` (so it rides the existing
`make cook VALIDATE=1`) and in `tools/assetlint.py`:

- every `path` resolves through `eng::assets::resolve`;
- the file **parses** — `luaL_loadbuffer` on the source, reporting the Lua
  syntax error with file and line. This is the reason the editor links
  `eng_script`;
- the chunk is **not executed** at cook time: validating a script must not
  require a world;
- every `Entity`-typed prop names an entity that exists in the scene.

`assetlint` gains a `.lua` pass mirroring its mesh and material resolution, so a
dangling script path fails the build exactly as a dangling mesh already does.

---

## 9. Tests

Headless, following the `engine/tests/*Tests.cpp` convention. A World with no
attachments is real in this engine, which is what makes these cheap.

**`ScriptHostTests`** — `start` runs once, on the tick *after* attach; `update`
receives dt; `on_destroy` fires on `destroyHierarchy`; several scripts on one
entity run in author order; props arrive on `self.props` with the right Lua
types; a disabled script does not tick.

**`ScriptBindingTests`** — position round-trips through the local `Transform`
and marks the subtree dirty; `world_position` is read-only; the reflection proxy
reads and writes `Spin`; a handle to a destroyed entity reports `valid == false`
instead of crashing; **the field proxy survives an `emplace` that moves the
pool** — the pointer-invalidation rule asserted directly rather than trusted.

**`ScriptErrorTests`** — a syntax error is reported at load, not at tick; a
runtime error's traceback contains the logical path and the line number; the
instance is quarantined after one error and does not run again; other instances
of the same script keep running; revive restores it.

**`ScriptReloadTests`** — rewriting the file swaps behaviour, keeps `self` state,
does not re-run `start`, fires `on_reload`, and revives a quarantined instance;
a file that fails to parse leaves the previous class live.

**`ScriptSerializeTests`** — `Scripts` round-trips through the byte serialiser
with all five prop types and several scripts per entity; a payload written
before a trailing field decodes with defaults.

**`ScriptContactTests`** — multiple contact callbacks each fire;
`removeContactCallback` stops one without affecting the others; a sensor
collider dispatches `on_trigger` and a solid one `on_collision`.

`layering` and `assetlint` cover the new paths once their rules are added.

Plus the on-screen check `CLAUDE.md` requires: a scripted entity in a scene, run
`make screenshot`, and read the PNG — a change that compiles is not a change
that works.

---

## 10. Documentation

- **`docs/scripting.md`** — new, the primary document. Object model (a script is
  a table, an instance is `self`), the lifecycle and the exact tick order, the
  full API reference, props, errors and tracebacks, hot reload, console
  commands, and a concrete **"how to add a script"** tutorial.
- **`docs/ecs.md`** — `Scripts` and `ScriptState` in the component table, and a
  note that `ScriptHost` is the deliberate stateful exception to the
  free-function `(World&, dt)` system shape, with the reason.
- **`ARCHITECTURE.md`** — `eng_script` in the layer table.
- **`docs/scene-editor-entities.md`** — authoring scripts and props in the
  inspector.
- Headers carry the same commentary density as the rest of the engine: what the
  type is for and why it is shaped that way, not a restatement of its members.

Sample scripts under `assets/scripts/`, which double as the tutorial's worked
examples:

- `spin.lua` — the minimum: `update` writing the entity's rotation.
- `lever.lua` / `door.lua` — an `Entity` prop and `e:script()`, the level-logic
  pattern.
- `trigger_volume.lua` — `on_trigger` on a sensor collider.

---

## 11. Files

### New

```
cmake/Dependencies.cmake                        (edited: lua, sol2)
engine/include/eng/ecs/components/Scripts.h
engine/include/eng/script/ScriptHost.h
engine/include/eng/script/ScriptState.h
engine/include/eng/script/ScriptConfig.h
engine/src/script/ScriptHost.cpp
engine/src/script/ScriptInstance.cpp          instance pool, lifecycle dispatch
engine/src/script/ScriptChunkCache.cpp        load, cache, reload
engine/src/script/ScriptError.cpp             traceback handler, reporting, quarantine
engine/src/script/bind/BindWorld.cpp
engine/src/script/bind/BindEntity.cpp
engine/src/script/bind/BindComponents.cpp     the reflection proxy
engine/src/script/bind/BindMath.cpp           vec3
engine/src/script/bind/BindInput.cpp
engine/src/script/bind/BindPhysics.cpp
engine/src/script/ScriptContactBridge.cpp
engine/src/script/ScriptConsole.cpp
engine/tests/ScriptHostTests.cpp
engine/tests/ScriptBindingTests.cpp
engine/tests/ScriptErrorTests.cpp
engine/tests/ScriptReloadTests.cpp
engine/tests/ScriptSerializeTests.cpp
engine/tests/ScriptContactTests.cpp
assets/scripts/spin.lua
assets/scripts/lever.lua
assets/scripts/door.lua
assets/scripts/trigger_volume.lua
docs/scripting.md
```

### Modified

```
CMakeLists.txt                          eng_script target + PCH, eng links it
                                        whole-archive, scene_editor links it for
                                        validation, new tests registered
tools/check_layering.py                 two path rules
tools/assetlint.py                      .lua resolution pass
engine/src/ecs/ComponentRegistry.cpp    Scripts, id 33
engine/include/eng/Physics.h            add/removeContactCallback
engine/src/physics/Physics.cpp          multi-subscriber contact dispatch
game/src/main.cpp                       migrate to addContactCallback
game/tests/PhysicsTests.cpp             migrate to addContactCallback
game/src/MapPlay.cpp                    host construction + tick order
game/src/LiveLevel.cpp                  host construction + tick order
editor/src/ui/ComponentInspector.cpp    Scripts block
editor/src/content/SceneSource.cpp      read [[entity.script]]
editor/src/content/SceneWriter.cpp      write [[entity.script]]
editor/src/content/SceneValidate.cpp    path + syntax + entity-prop checks
assets/schemas/scene.schema.json        script array shape
assets/assets.toml                      scripts/ in the directory map comment
ARCHITECTURE.md                         eng_script in the layer table
docs/ecs.md                             Scripts / ScriptState
docs/scene-editor-entities.md           authoring scripts
```

---

## 12. Implementation order

Six stages, each ending somewhere the tree builds and the tests pass, so the work
stays reviewable and a stall never leaves the repository half-converted.

1. **Build plumbing.** Lua and sol2 in `Dependencies.cmake`, the `eng_script`
   target with its PCH, the two layering rules. Ends with an empty library that
   links and a `layering` run that passes.
2. **Component and serialisation.** `Scripts`, `ScriptState`, registry entry id
   33, `ScriptSerializeTests`. No VM involved yet.
3. **Host and lifecycle.** `ScriptHost`, the chunk cache, the instance pool,
   `start`/`update`/`fixed_update`/`on_destroy`, the traceback handler and
   quarantine. `ScriptHostTests` and `ScriptErrorTests`.
4. **Bindings.** `vec3`, the entity handle, the reflection proxy, `world`,
   `log`, `input`, `physics.raycast`, `event`. `ScriptBindingTests`.
5. **Integration.** Contact callback multiplexing and both migrations, the
   contact bridge, hot reload, console commands, and host construction plus tick
   order in `MapPlay` and `LiveLevel`. `ScriptContactTests`, `ScriptReloadTests`,
   and the on-screen screenshot check.
6. **Authoring.** Inspector block, `.scn` read/write, JSON schema, cook
   validation, assetlint pass, sample scripts, and the documentation.

Stages 1–4 touch no existing behaviour at all: the first change to shipped code
is the contact-callback migration in stage 5.

## 13. Risks

**sol2 compile time on a tree that never clean-builds.** Mitigated by the
dedicated PCH and the per-module binding split. If a single binding TU still
becomes slow to iterate on, split it further rather than reaching for unity
builds — batching translation units merges anonymous namespaces, which this
repository already documents as a hazard.

**Lifetime across the C++/Lua boundary.** The two handle invariants in §5, each
with a dedicated test, are the whole defence. A binding that returns a raw
pointer or a reference into a component pool is the failure mode to watch for in
review.

**Calling Lua from an entt destroy signal.** `on_destroy<ScriptState>` fires
while the entity is still valid, but the registry is mid-mutation. Spawns are
refused during teardown, and destroys are already queued.

**Prop type inference from TOML.** A number authored as `2` and one authored as
`2.0` both become `Number` (a `double`); scripts never see an integer type. This
is stated in the docs rather than papered over.

**Editor and runtime disagreeing about a script path.** The cook-time resolution
and syntax check exist precisely so this fails at build time, like a dangling
mesh, rather than as a silently inert entity.
