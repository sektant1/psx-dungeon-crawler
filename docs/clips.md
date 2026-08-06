# Clips: short authored animations

A door that opens over 0.8 s, a platform that rises, a light that ramps when a
lever is pulled, a camera that pushes in for a shot. This is the system for
those.

For the skeleton — actors, locomotion, attacks — see
[actor-animation.md](actor-animation.md); that is ozz's job and this does not
touch it. For where clips sit in the wider redesign see
[design/2026-08-06-scene-contract-and-one-component-standard.md](design/2026-08-06-scene-contract-and-one-component-standard.md).

## What was missing

The engine had two kinds of animation and no middle.

| | What it does | Why it was not enough |
|---|---|---|
| `Spin`, `Orbit`, `LightAnimation` | endless procedural modulation | no beginning, no end, one behaviour each |
| ozz clips | skeletal, blended, masked | needs Blender and `gltf2ozz` to author a frame |

Everything between the two was a Lua script written per instance. A door, a
platform, a ramping light and a camera push are the same animation with
different numbers, and they were four scripts.

## The idea

A `Clip` is a duration and a list of **tracks**. A track names a component and
a field **as strings** and drives them over time:

```json
"Clip": {
  "duration": 0.8,
  "mode": "once",
  "autoplay": true,
  "tracks": [
    { "component": "Transform", "field": "position", "ease": "smooth",
      "keys": [ { "t": 0.0, "v": [0, 0, 0] }, { "t": 0.8, "v": [0, 3.2, 0] } ] }
  ]
}
```

The names are resolved once, through the same `ComponentRegistry` that drives
`.map` serialisation and the inspector. That is the whole trick, and it has one
consequence worth stating plainly:

> **Every reflected field in the engine and the game is animatable the day it is
> declared.** A light's range, a shader's tint, a camera's fov, a portal's
> parameters — with no per-component animation code, ever.

`ClipSystem.cpp` does not mention `ShaderParams`, and a test animates
`ShaderParams::tint` to prove it.

## Authoring it: the Timeline panel

In the **game**: `F1` ▸ **Timeline** (World group).
In the **scene editor**: the **Timeline** tab along the bottom, beside Problems
and Console — where every DCC puts a timeline, and for the same reason: it is
read against the viewport above it while something plays.

One limitation stated plainly: the editor's Timeline drives the **preview**
world, which is rebuilt from the document on every edit. So it *plays* and
*scrubs* a clip authored into a scene, and a key retimed there is lost on the
next rebuild. Authoring a clip into the `.scn` needs the `Clip` component in the
inspector, which is the next step and is **not built**. Previewing is still the
half that cannot be done any other way — a timeline you cannot play is not a
timeline.

- Pick an entity carrying a `Clip` from the combo, or press **New clip on
  selected**.
- **Add track** — the two dropdowns *are* the component registry, filtered to
  the field types a clip can interpolate. There is no hardcoded list, so a
  component reflected tomorrow appears in them tomorrow.
- **Drag a diamond** to retime a key; **right-click** one to delete it. Keys
  re-sort on release rather than mid-drag, so the key under the cursor stays the
  key under the cursor.
- **Key at playhead** takes the value each tracked field has *right now* — the
  pose-then-key workflow. It works only because the field is reachable
  generically: the panel reads it through the same registry the player writes it
  through.
- Scrubbing pauses playback, because a playhead that fights you for the position
  is the most irritating thing a timeline can do.

A track whose names did not resolve is drawn in red on its own row, and warns
once in the log. Silence about a typo is the failure mode that costs an
afternoon.

## The parameters

| Field | Means |
|---|---|
| `duration` | seconds; keys are in the same units, so retiming is one number |
| `mode` | `once` holds the last pose, `loop` wraps, `pingpong` reverses |
| `speed` | multiplier; negative plays backwards |
| `autoplay` | whether it starts on load. False for anything a trigger starts |

A track adds:

| Field | Means |
|---|---|
| `target` | empty = this entity; otherwise a **descendant** by `Name` |
| `component` / `field` | a registry type name and a reflected field name |
| `ease` | `linear`, `smooth` (default), `easein`, `easeout`, `step` |
| `keys` | `t` in seconds, `v` a vec3 — a Float track uses `v.x` |

Before the first key a track holds the first value and after the last it holds
the last, so a track covering only the middle of a clip is a legal way to say
"this part does not move yet".

## Deliberate limits

These are the boundary that keeps a 300-line clip player from becoming a second
animation runtime beside ozz:

- **No blending between clips.** One clip per entity, played or not.
- **No curve editor.** A key holds a value; a track holds one easing. A bezier
  handle per key is where the second runtime starts.
- **No `Quat` interpolation.** Rotation is authored as an euler `Vec3`, which is
  what the inspector already shows. Naming `Transform.rotation` in a track warns
  and says so.
- **Targets are the entity or a descendant.** A clip reaching across the scene
  is a dependency the scene file does not record, and it breaks the moment
  either entity is duplicated. Two entities that must animate together are two
  clips, or a script.
- **No timeline events.** A `once` clip finishing is the one signal.

Anything past this line wants a skeleton, and the skeleton already has ozz.

## What is *not* saved

`time`, `playing`, `started`, `finished`, `direction` and `appliedTime` are
runtime state — where the clip currently *is*, not how it was authored — and
follow the same rule as `Orbit::travelled` and `LightAnimation::time`. A saved
playhead would reload mid-swing.

`finished` latches when a `once` clip reaches its end and is **not** cleared by
the player, which is what makes it observable: "has this door finished opening"
is a question gameplay asks, and a flag consumed on the frame that set it could
never answer it. The transport's Play and Stop clear it.

`appliedTime` is the playhead the current pose was written from. It is what
decides whether a frame has work to do, and it replaces the obvious `playing`
test for two reasons: a *stopped* clip whose time was moved (scrubbed in the
Timeline, seeked by a script) must still re-pose, and a *finished* one must not
— it writes a `Transform`, which tags the subtree `Dirty`, which would keep the
hierarchy resolving that branch forever for something that has not moved.

The resolved indices are omitted for a stronger reason: they are offsets into a
registry that the loading build may have numbered differently, so persisting
them would silently animate the wrong field.

## Wiring it up

`clipSystem` resolves components by name, so it needs the type table:

```cpp
eng::ecs::ComponentRegistry types;
eng::ecs::registerEngineComponents(types);
// ... the application adds its own from kFirstApplicationTypeId up

world.setComponentTypes(&types);       // must outlive the World
```

A World without one still works — `clipSystem` no-ops — which is what keeps the
headless combat sim and the map tests free of the registry.

It runs inside `tickComponentSystems`, **after** `Spin`, `Orbit` and
`LightAnimation`. That order is the contract: a clip is the more specific
statement about a field, so it gets the last word on any they share.

A clip that writes a `Transform` tags the entity `Dirty` itself. The hierarchy
resolve is driven by `Dirty`, not by comparing poses, and a clip is the only
thing in a frame that can move an entity without going through
`World::setLocalTransform`.

## See it working

```sh
make scene SCENE=clip_demo.scn
```

`assets/scenes/clip_demo.scn` is three clips side by side, and each one makes a
different point:

| Entity | Track | Point |
|---|---|---|
| **Riser** | `Transform.position`, `once` | the door/platform case: plays, then holds |
| **Pacer** | `Transform.position`, `pingpong` | the same track, a different mode |
| **Pulser** | `ShaderParams.tint`, `loop` | a component `ClipSystem.cpp` never mentions |

The third is the one worth watching. Nothing in the clip player knows
`ShaderParams` exists — it is animated because it is reflected.

## Adding an animatable component

There isn't a step. Reflect the component the normal way
([ecs.md](ecs.md) § "Adding a component") and it is animatable — it appears in
the Timeline's dropdowns and a track can name its fields. That is the property
the whole design was for.

## Where a mode or an ease is defined

One place: `eng/ecs/components/Clip.h`, which carries both registers —

- `kClipModeNames` / `kClipEaseNames`, the display strings, free to be reworded;
- `kClipModeIds` / `kClipEaseIds`, the on-disk ids, which are a **file format**
  and are not.

Plus `clipModeId` / `clipModeFromId` (and the ease pair) for the `.scn` reader
and writer. Before this the labels were duplicated in the Timeline and the
inspector, and the ids were split between the reader's if-chain and the writer's
switch — so a mode added to the enum could ship as a dropdown missing its last
row, or as a scene that saves and then refuses to load. `static_assert`s tie the
tables to the enums, and `scene_roundtrip` sweeps every mode × every ease.

## Tests

`ecs_systems` covers: a linear track sampled halfway, `once` stopping exactly on
the last key and holding, `loop` wrapping a seven-second frame with `fmod`, a
misspelled field animating nothing rather than something else, `autoplay=false`
waiting to be started, a component `ClipSystem.cpp` never mentions being
animated through reflection, a World with no type table not crashing, a stopped
clip re-posing nothing (no perpetual `Dirty`), and a scrub on a paused clip
re-posing anyway.

`scene_roundtrip` covers the authored side: every field surviving a write and a
read, defaults being omitted, scalar `v` shorthand, and the enum sweep above.
