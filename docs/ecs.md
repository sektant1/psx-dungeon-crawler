# The ECS {#doc-ecs}

What an object *is* in this engine, who owns its lifetime, and how to add a
component. Follows *Game Engine Architecture* (Gregory, 4th ed.) vol. I §1.5.15
— the questions a game object model has to answer — and the "RTTI / Reflection
& Serialization" support system in §1.5.7.

For the layering see [ARCHITECTURE.md](../ARCHITECTURE.md); for clocks, string
ids and profiling see [engine-foundations.md](engine-foundations.md).

## The object model

§1.5.15.1 lists the questions. Answered once, here, rather than once per
subsystem:

| Question | Answer |
|---|---|
| What is an object? | One `entt::entity` in one `eng::ecs::World`. |
| How is it identified? | By entity id within its World; by `Name` for humans, `StringId` in a hot path. |
| How is it referenced? | By entity id, checked with `registry().valid()`. **Never** by a component pointer held across a frame — any `emplace` can move a pool. |
| Who owns its lifetime? | The World. `destroy()` unlinks it; the next `sync()` frees its node and body. |
| How is it simulated? | By systems taking `(World&, dt)` and iterating views. Not by a virtual `update()`. |

### One World per simulated world

This is the invariant the type exists to enforce. Before it a level had **three**
registries — scene actors, authored map entities, combatants — so an enemy was
one entity in one and (at best) another in the next, no view could see both
halves, and anything wanting the whole object (a tooltip, a save file, an
inspector) joined them by hand. Every new system had to pick a side.

A *different* World is still fine: the editor's material preview owns one
because it is a different simulation, not half of this one.

### Lifetime groups

A World outlives the levels streamed through it — the player is not the level's
to destroy. So entities carry an `EntityGroup`:

```cpp
world.setActiveGroup(kLevelGroup);   // everything created from here is the level's
... build the level ...
world.setActiveGroup(0);             // back to persistent

world.destroyGroup(kLevelGroup);     // a transition: exactly the level's entities
```

Group 0 means "lives as long as the World" and `destroyGroup(0)` is refused —
it would take the survivors with it, which is the bug the mechanism exists to
prevent. This is the only lifetime policy the engine imposes.

### Attachments

The renderer and physics are **views** of the registry, driven by reconcilers.
Explicit start-up ordering rather than a constructor, which is §6.1.2's
recommendation and for its reason — both subsystems must exist first:

```cpp
eng::ecs::World world;
world.attachRenderer(backend);   // SceneSync
world.attachPhysics(physics);    // PhysicsSync
...
world.sync();                    // transforms -> renderer -> physics, in that order
world.detachAll();               // before the renderer or physics world dies
```

Both are optional, and that is what makes a headless World real: the combat sim
and the map tests attach neither and run the same systems the game runs.

### What is *not* in the World yet

- **The batched dungeon shell** (`DungeonMap`) — region-batched static geometry
  is a different path from per-entity actors, as in any shipping engine.
- **Combatants** (`CombatDirector`'s registry) — the next system to move across.

Both are stated here rather than left to be discovered.

## Components

One file each, under `engine/include/eng/ecs/components/`.
`eng/ecs/Components.h` includes the lot; include the single header when you want
one, so a file needing `Transform` does not pull Jolt's layer enum in through
`Collider`.

| Component | Means |
|---|---|
| `Name` | human-readable label |
| `Transform` | local pose — the authored value |
| `WorldTransform` | composed pose — derived, never set |
| `Dirty` | this subtree needs re-resolving |
| `Parent` / `Children` | hierarchy, both ends kept in step by `setParent` |
| `EntityGroup` | lifetime group |
| `Lifetime` | seconds left; the subtree is destroyed at zero |
| `RenderNode` | give me a node though I draw nothing |
| `MeshRenderer` | a mesh to draw |
| `MeshSource` | the path it was loaded from |
| `PrimitiveMesh` | …or the parameters it was generated from |
| `MaterialOverride` | draw it with a different material |
| `ShaderParams` | tint, rim light and cutout, for this entity alone |
| `Visibility` | drawn or not — hiding is not destroying |
| `Camera` | look through me: fov, clip planes, priority |
| `LightAnimation` | how a light flickers or pulses |
| `Spin` | turn forever about a local axis |
| `Orbit` | travel a ring around a point — no pivot entity |
| `ParticleEmitter` | play this effect from here |
| `Collider` | a shape that occupies space |
| `RigidBody` | …and the simulation moves it |
| `KinematicControl` | …but gameplay steers it |
| `Clip` | a short authored animation over reflected fields — see [clips.md](clips.md) |
| `Scripts` | Lua behaviours attached here: paths plus per-instance props |
| `NodeRef` / `BodyRef` / `ParticlesRef` / `MaterialApplied` / `ScriptState` | runtime handles, written by reconcilers and the script host, never by callers |

Two splits worth the words:

**`Collider` vs `RigidBody`.** A Collider says *what shape*; a RigidBody says
*whether it moves*. So a prop that falls over is one component added to
something that already collides, not a different kind of object. A dynamic
body's pose then flows *out* of physics — PhysicsSync stops writing the
Transform onto it — unless `KinematicControl` says gameplay is steering.

**`MeshRenderer` vs `MeshSource` vs `PrimitiveMesh`.** A `MeshRenderer` holds a
`MeshHandle` and nothing else, deliberately: resolving an asset is a load-time
job and the hot path must not carry a path. What it does *not* say is where that
handle came from, and there are two answers — a `MeshSource` names a file, a
`PrimitiveMesh` describes geometry the engine builds (box, sphere, capsule,
cylinder, cone, plane, disc, beveled box). An entity carrying both is a mesh
file; the path wins, because a file is the more specific statement.

Resolution is one seam, `eng/ecs/MeshResolve.h`, so the three consumers — the
game's map loader, the editor's preview and the demo — cannot grow three subtly
different answers. `PrimitiveMeshCache` keys on the parameters, so a hundred
identical greybox blocks share one vertex buffer, and one cache per level means
tearing a level down releases exactly the meshes that level generated.

**`MeshRenderer` vs `MaterialOverride` vs `ShaderParams`.** The mesh and its
material come from the asset. An override is a scene decision — *this* pillar is
the cracked variant — so it names a different material. `ShaderParams` changes
nothing about *which* material: it sets uniforms on the one the entity already
wears. Three different authors, three rates of change, three components.

The last is the only component in the engine whose cost is a draw call. Ogre
materials are shared by name, so `setMaterialParam` tints all hundred and sixty
walls at once; making one of them glow means giving it a private copy. SceneSync
clones on the first push and reuses the clone after, and removing the component
puts the shared material back — so the price is paid by the handful of hero
objects that ask for it and by nothing else. The inspector says so where the
decision is made.

### Behavioural components and their systems

Four components do something over time. Each has one system in
`eng/ecs/Systems.h`, taking `(World&, dt)`:

| Component | System | What it does |
|---|---|---|
| `Spin` | `spinSystem` | rotates the local `Transform`, so the subtree turns with it |
| `Orbit` | `orbitSystem` | writes the position on a ring, and the facing when it is aiming |
| `LightAnimation` | `lightAnimationSystem` | writes `LightColour` from the `LightRef`'s authored colour |
| `Clip` | `clipSystem` | plays a short authored animation over reflected fields |
| `Lifetime` | `lifetimeSystem` | counts down, then `destroyHierarchy` |
| `Visibility` | *(SceneSync)* | pushed to the node when it changes |

`clipSystem` is the one that addresses its target by **name** rather than by C++
type: a track says `"Transform"`/`"position"` and resolves it through the
`ComponentRegistry`. That is why `World::setComponentTypes()` exists, and why
every reflected field is animatable the day it is declared. See
[clips.md](clips.md).

`tickComponentSystems(world, dt)` runs the ones that need a clock, in the order
a frame wants them — the three procedural modulators, then clips (the more
specific statement about a field, so it gets the last word), then lifetimes. Call it **before** `World::sync()`; `sync()` deliberately
does not, because the editor syncs its preview world every frame and would
otherwise watch authored entities spin and expire while placing them.

### The one system that is not a free function

`eng::script::ScriptHost` is a stateful object, and deliberately so. A system's
contract here is a single `(World&, dt)` call; this host's callbacks land at
**three** different places in a frame — `fixed_update` before the physics step,
contacts after it, `update` with presentation — and a generic `update(dt)` would
hide the one thing a reader needs to know about it. It also owns a Lua state and
a pool of live instances, which is not something a free function can carry.

It follows the same authored-versus-derived split as everything else: `Scripts`
is the authored component, `ScriptState` is the runtime handle the host writes.
See [scripting.md](scripting.md).

Two rules the free-function systems follow, and any new one should:

- **Modulation reads the authored value and writes a derived one.**
  `lightAnimationSystem` computes `LightColour` from `LightRef::desc.colour`
  every frame. Scaling `LightColour` in place instead would compound and fade a
  torch to black in seconds.
- **No global RNG.** The flicker is value noise over accumulated time plus a
  per-instance `phase`, so two runs of a level light it identically and a
  capture is comparable frame for frame.

### Spin vs. Orbit

`Spin` turns a thing where it stands. `Orbit` moves it along a ring around a
point — and it needs **no pivot entity** to do it: the entity carries its own
`centre`, `radius` and `axis`.

The pivot rig (an empty entity with a `Spin`, children parented to it) is still
right when a *group* has to revolve as one, because a parent is how several
things share a motion. `Orbit` is the single-entity case, which is nearly every
case. It was two entities and a parent link for one moving camera, and the
radius *was* the child's transform — a number that did not say what it was.

They compose on one entity, and the order in `tickComponentSystems` is the
contract that lets them:

```
spinSystem    accumulates a rotation
orbitSystem   writes a position, and REPLACES the rotation only when facing != Free
```

So a moon is `Orbit(facing: Free) + Spin` — carried round its planet while
turning on its own axis — and a camera is `Orbit(facing: Centre)`, whose aim
Spin cannot fight over.

`facing` has three values: `Free` keeps the authored rotation, `Centre` looks at
what it circles, `Travel` looks along the direction of motion. `centre` is in
the entity's own frame — its parent's, or the world — so an orbit inside a rig
moves with the rig.

`travelled` is accumulated runtime state and deliberately not reflected: it is
where the entity currently *is*, not how it was authored, and a saved one would
reload mid-arc. Same rule as `LightAnimation::time`.

### The camera is an entity

`Camera` is the lens (`fovDegrees`, `nearClip`, `farClip`, `priority`,
`active`); *where* it looks from is the entity's transform, like everything
else. SceneSync picks the active camera with the highest priority and pushes it.

That is what makes a shot authorable. An orbiting camera is a `Camera` parented
to a pivot carrying a `Spin` — two components, no C++ — and the same rig serves
a cutscene, a menu backdrop or a ten-second clip. A debug or death cam takes
over by existing at a higher priority; neither camera knows about the other.

Two properties worth knowing:

- **A world with no `Camera` never touches the renderer's camera.** A game that
  drives its own (every dungeon level does) is unaffected.
- **With one, the attachment is re-asserted every frame.** The renderer has
  exactly one camera and anything may attach it — a player controller does when
  it spawns, which happens *after* the scene loaded. Attaching once would mean
  the scene's camera silently loses. The lens is still pushed only on a change.

`MapPlay` reads this as a mode: a map whose registry contains a `Camera` plays
as a *shot* — the player controller stands down and the mouse stays free, which
is also what makes it recordable with `--record` (see
[authoring-shots.md](authoring-shots.md)).

### Which entities cost the renderer anything

Only those that ask: `MeshRenderer`, `LightRef`, `ParticleEmitter`, or a bare
`RenderNode` tag. Not merely having a `Transform` — once one World holds the
whole game, most entities in it (a combatant's stats, a spawner, a trigger
volume) have a position and nothing to draw, and a node each would be a node and
a transform push per frame for nothing.

## Adding a component

Three steps. The point of the reflection layer is that there is no fourth.

**1. Write the header.** `components/Ripple.h`:

```cpp
#pragma once
namespace eng::ecs {

// What it means, and who writes it. That comment is the design.
struct Ripple {
    float amplitude = 0.2f;
    float speed = 1.0f;
    bool enabled = true;
};

} // namespace eng::ecs
```

Add it to the list in `Components.h`. No base class, no factory, no virtual —
which is the property that makes a component cheap enough to add for one
entity's sake.

**2. Declare its fields** in `ComponentRegistry.cpp`:

```cpp
template <> FieldSpan fieldsOf<Ripple>()
{
    static const Field f[] = {
        ENG_FIELD_RANGE(Ripple, amplitude, FieldType::Float, 0.0f, 2.0f),
        ENG_FIELD_RANGE(Ripple, speed,     FieldType::Float, 0.0f, 10.0f),
        ENG_FIELD(Ripple, enabled, FieldType::Bool),
    };
    return {f, int(std::size(f))};
}
```

and register it with the next free stable id:

```cpp
reg.add(reflectedComponent<Ripple>("Ripple", 10));
```

That is the whole registration. The `.map` payload, the inspector rows and the
add-component menu all read the same table, so they cannot drift — which is the
failure mode reflection exists to remove. Without it a new component is a
serialiser, a deserialiser, a drawer and a menu row: four places to forget, and
the forgotten ones fail *silently* (it saves but does not load; it loads but
cannot be edited).

`ENG_FIELD` takes the member token, so the label in the UI and the member it
edits are the same identifier and cannot diverge.

**3. Make something happen.** If the component needs an effect in the world,
that belongs in a reconciler (`SceneSync`, `PhysicsSync`) or a system — never in
the component. A component is data.

### Rules for stable ids and fields

- **Stable ids are a file format.** Never reuse one, never renumber. Engine ids
  live below `kFirstApplicationTypeId` (10); the game numbers from there up.
- **Append fields, never reorder or remove.** Decoding stops when the payload
  runs out, so an older file loads into a newer component with the new fields at
  their defaults. Reordering reinterprets every payload already on disk.
- A component whose members are not all POD — a handle a reconciler writes back,
  a `shared_ptr` to a definition — declares only the fields that are, or keeps a
  hand-written serialiser. `MeshRenderer` and `LightRef` do the latter, because
  their payloads are already shipped.

### Loading without destroying

`copyEntities(dst, src, types, group)` merges a parsed registry into a live one,
carrying every registered component and rebuilding the hierarchy through a
remap. It is what `MapRuntime::load` uses.

The predecessor was `registry = std::move(parsed)`, which was fine while a map
owned the only registry in the level and is destructive now that it does not —
the swap would take the player, the enemies and every other live entity with it.

Entity ids are **not** preserved: the copies are fresh. That is safe only
because no serialisable component stores an entity id. If one ever does it needs
a remap entry, and `copyEntities` is where.

## Tests

| Test | Covers |
|---|---|
| `scene_tests` | hierarchy, transform resolution, cycle rejection, groups |
| `scene_sync` | node materialisation rules, attach-once, teardown |
| `physics_sync` | body lifetime, rebuild on shape change |
| `component_reflect` | stable-id uniqueness, field offsets, round-trips, truncation, uniform add/remove |
| `map_runtime` | merge-not-replace, group teardown, corrupt-file rejection |
