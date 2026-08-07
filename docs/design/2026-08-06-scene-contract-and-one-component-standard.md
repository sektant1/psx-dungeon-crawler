# Scenes: one component standard, a scene contract, and short animations {#doc-design-2026-08-06-scene-contract-and-one-component-standard}

What a scene *is*, what it must carry to work, how a component gets from a
header to a `.scn` file, and how to animate something for eight tenths of a
second. Written 2026-08-06 after reading the four paths that currently disagree.

For the layering see [ARCHITECTURE.md](../../ARCHITECTURE.md); for the object
model see [ecs.md](../ecs.md); for the pipeline see
[assets-pipeline.md](../assets-pipeline.md).

---

## 1. The diagnosis

The ECS is not the problem. `eng::ecs::World` is one registry, components are
data, `fieldsOf<T>()` reflects them, and `.map` serialisation is generic. That
part is right and this design does not touch it.

The problem is that **the authored side of a component is written four more
times, by hand**, and none of the four knows about `fieldsOf<T>()`.

Adding one authored component today — measured on `ThirdPersonCamera`, which is
a *mirror* component and therefore the cheap case — touches **nine files**:

| # | File | What is written by hand |
|---|---|---|
| 1 | `engine/include/eng/ecs/components/ThirdPersonCamera.h` | the struct |
| 2 | `engine/src/ecs/ComponentRegistry.cpp` | `fieldsOf<>` + `reg.add(...)` |
| 3 | `assets/schemas/scene.schema.json` | a `$defs` entry and an entity property |
| 4 | `editor/include/editor/content/SceneDocument.h` | `using ThirdPersonAuthor = …` + `std::optional<…>` member |
| 5 | `editor/src/content/SceneSource.cpp` | JSON → struct, field by field |
| 6 | `editor/src/content/SceneWriter.cpp` | struct → JSON, field by field |
| 7 | `editor/src/content/SceneCook.cpp` | `built.emplace<…>(entity, *authored.thirdPerson)` |
| 8 | `editor/src/ui/ComponentInspector.cpp` | `drawThirdPerson()`, a bespoke ImGui drawer |
| 9 | `editor/src/scene/EntityComponents.cpp` | the add/remove/has table row |

Steps 3, 5, 6 and 8 are pure restatements of information step 2 already holds.
`ComponentInspector.cpp` is **2015 lines** of drawers, of which exactly one
(`drawPortal`) reads `fieldsOf<>` and the other thirty-odd retype the field list
in ImGui calls. `SchemaSyncTests` exists specifically because the schema rots
away from the writer — a test that would be unnecessary if there were one
description instead of two.

That is the "not in the same standard" the request names, and it is what makes
the format feel arbitrary from the outside: `player_spawn` is a bare `bool`,
`enemy_spawn` is a bare `string`, `third_person` is a nested object, `actor` is
an enum, `marker` is a string that the validator *reinterprets* as an enemy —
because each was added by whoever added it, in the shape that was convenient
that day. The schema's entity object has **35 sibling keys** with no rule
relating them.

Two more gaps the same reading turned up:

- **Nothing says what a scene needs.** `validate()` has 39 issue codes and every
  one of them is about an entity being wrong. There is no code for *a scene with
  no camera and no player spawn*, which loads, cooks, plays, and shows nothing.
  The one requirement that does exist (`exit.missing`) is a dungeon rule, not a
  scene rule.
- **There is no way to animate anything for a fixed duration.** `Spin`, `Orbit`
  and `LightAnimation` are endless procedural modulators. ozz clips are skeletal
  and need Blender plus `gltf2ozz`. Everything in between — a door that opens
  over 0.8 s, a platform that rises, a camera that pushes in for a shot, a light
  that ramps up when a lever is pulled — is a Lua script, hand-written per
  instance.

---

## 2. The decisions

### D1 — The reflected component registry is the only description of a component

`fieldsOf<T>()` already drives `.map` encode/decode. It becomes the source for
the other four consumers too:

```
                      fieldsOf<T>()  ← the ONE description
                            │
        ┌──────────┬────────┼────────┬──────────────┐
        ▼          ▼        ▼        ▼              ▼
   .map codec   .scn read  .scn write  inspector  scene.schema.json
   (exists)      (new)      (new)       (new)      (generated)
```

Adding a component becomes **two files**: the header, and the registration line.
Steps 3, 5, 6, 8 and 9 above disappear; step 7 becomes a generic loop.

The mechanism is `ed::ComponentJson` (`editor/src/content/ComponentJson.{h,cpp}`)
— `readFields`, `writeFields`, `schemaFor`, over a `eng::FieldSpan`. It lives in
the editor layer, not `eng_core`, because nlohmann/json is an *authoring*
dependency and the engine reads `.map`; putting it lower would drag JSON into
every headless test for no runtime gain.

**Only fields that differ from a default-constructed instance are written.** A
`.scn` then says what the author decided and nothing else, diffs stay readable,
and adding a field to a component does not rewrite every scene in the repo.

**Custom drawers stay possible and stay the exception.** A field that needs an
asset picker, a dropdown of kit ids, or a curve is registered as an override
keyed on `(component, field)`. The default is the generated grid; the override is
opt-in and is a code smell budget, not a habit.

**The JSON schema becomes generated**, emitted by a tool from the live registry
and checked in. `SchemaSyncTests` then stops comparing two hand-written things
and starts asserting the checked-in file matches what the registry generates —
which is a real guarantee rather than a coverage exercise.

#### The migration is per-component and reversible

`ed::Entity` grows a type-erased `components` bag beside its existing optional
members. Reader, writer and cooker consult the bag *and* the legacy members, so
a component moves across one at a time and every `.scn` on disk keeps loading
byte for byte. The optional members are deleted when the last one has moved.
This is why the design does not require a flag day.

#### What does *not* move

`Name`, `Transform`, `MeshRenderer` and `LightRef` keep hand-written codecs —
their `.map` payloads are shipped and their members are not all POD. `Scripts`
and `Properties` are variable-length by nature. These five are named here so the
next reader does not think they were missed.

### D2 — A scene declares its contract, and the contract is checkable

A scene is not "a list of entities". It is a list of entities that fills a set
of **roles**. Stating the roles is what makes "I don't know what does what in
the scene" answerable — by the editor, out loud, per scene.

```
role            filled by                      required?
────────────────────────────────────────────────────────────────
view            Camera │ FirstPersonController │ ThirdPersonCamera │ ScreenCamera
                                              exactly one, always
spawn           PlayerSpawn                    when the view is a player view
listener        AudioListener                  at most one; warn on none
environment     SceneEnvironment               optional; defaults apply
key light       LightRef(directional)          warn on none in a world scene
exit            Exit                           dungeon levels only
```

Three properties make this worth building rather than writing in a README:

- **It is one table**, `sceneContract()`, read by the validator, the cooker, the
  editor's panel and the game's loader. Four consumers cannot disagree about
  what a scene needs.
- **Every unfilled required role has a quick fix**, so "this scene has no
  camera" comes with the button that adds one. That is the same `QuickFix`
  mechanism `validate()` already has.
- **A scene's *kind* is derived, not authored.** Which view component is present
  already decides whether the scene plays as a shot, a first-person level, a
  third-person level or a 2D screen — `MapRuntime` and `MapPlay` read exactly
  that today. The contract names the rule instead of leaving it implicit in two
  `if`s in different files.

The editor grows a **Scene** panel that is just this table rendered: each role,
what fills it, and a button when it is empty. That is the direct answer to "I
don't know what does what in the scene" — and it is also how adding or removing
a first- or third-person camera becomes one click, because "swap the view role"
is a legal operation on a contract and was a manual component surgery before.

### D3 — Short animations are a `Clip` component over reflected fields

The payoff of D1, and the reason to do D1 first.

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

A track names a **component and a field by string**, resolves them once through
`ComponentRegistry` + `fieldsOf<>`, and writes the value each frame. Because the
resolution is generic, *every reflected field in the engine and the game is
animatable the day it is declared* — a light's range, a shader's tint, a
camera's fov, a portal's parameters — with no per-component animation code, ever.

That is the whole argument for one standard, made concrete: a system nobody
wrote for `ShaderParams` animates `ShaderParams`.

Deliberate limits, so this stays a clip player and does not become a second
animation framework beside ozz:

- **Scalar-ish types only** — `Float`, `Vec3`, `Colour`, and `Bool`/`Int` as
  steps. `Quat` is not interpolated yet; rotation is authored as euler `Vec3` on
  the `Transform`, which is what the editor already shows.
- **No blending between clips.** One clip per entity, played or not. Blending is
  ozz's job and the layer where it belongs.
- **No root motion, no events on the timeline.** A clip finishing raises one
  event, which is enough for a door to unlock a trigger.
- **Targets are the entity itself or a named child.** No cross-scene addressing;
  a clip that has to drive two unrelated entities is two clips or a script.

Clips compose with what exists rather than replacing it: `Spin` stays the right
answer for endless rotation, ozz stays the right answer for a skeleton, and a
Lua script stays the right answer for anything with a decision in it.

### D4 — The DCC intermediate: partly, and here is the honest gap list

The question was whether any DCC model becomes one common internal
representation carrying only the data the engine needs. The answer today is
**yes for static geometry, no for everything else.**

What works: `eng::content::MeshData` / `.rmesh` is a real canonical intermediate.
Every format Assimp reads collapses into one struct — position, normal, tangent,
texcoord, colour, plus a collision triangle soup — baked to one orientation and
pivot by `importStaticModel()`, keyed on source bytes + import settings. That is
the row of Gregory's figure 1.33 working as designed.

What does not:

1. **Skinned meshes bypass the intermediate entirely.** `humanoid_rig.glb` is
   marked `skip = true` and the runtime loads the `.glb` directly through Assimp
   at start-up, because `MeshData` has no joint indices or weights. So the one
   asset class with the heaviest import cost is the one that gets none of the
   pipeline's benefit.
2. **The editor's model importer writes `.obj`, not `.rmesh`.** A GLB dropped in
   the editor is decomposed into one OBJ per submesh — through a lossy ASCII
   format that cannot carry vertex colour, tangents, or a second UV set — and
   those OBJs are then re-imported by the mesh row. Two conversions where the
   canonical form was available after the first.
3. **Nothing survives import except geometry.** No node hierarchy, no sockets or
   attachment points, no material binding (deliberately, and the reasoning in
   `ModelImportPipeline.h` is sound for *materials* — but it means a muzzle
   socket authored in Blender cannot reach the engine at all, which is why the
   viewmodel rig hardcodes offsets).
4. **`.animtree.toml` is a row in the format table with zero files.** It is
   advertised and unimplemented.

The fix is one format change plus two call-site changes, and it is sequenced
after D1–D3 because it is the least blocking of the four:

- `MeshData` gains optional `joints`/`weights` per vertex and a `sockets` list
  (name + transform, from the DCC's empties/nodes). Optional, so every existing
  `.rmesh` still decodes — the same append-only rule component payloads follow.
- `ModelImportPipeline` writes `.rmesh` directly.
- The skinned path reads `.rmesh` for geometry and keeps ozz for skeleton and
  clips. ozz stays the animation authority; this only stops the *mesh* taking a
  second route.

Then "any DCC model becomes one intermediate carrying what the engine needs" is
true without an asterisk, and the `skip = true` sidecars go away.

---

## 3. Sequencing

| Stage | What | Status |
|---|---|---|
| 1 | `Clip` + `clipSystem` over the reflected registry | **done** |
| 2 | The Timeline panel, in the game console and the editor's bottom rail | **done** |
| 3 | `sceneContract()` + the Scene section of the Problems panel | **done** |
| 4 | `Clip` authoring: `.scn` read/write, cook, schema, inspector | **done** |
| 5 | `ComponentJson` — drive `.scn` read/write/schema/inspector from `fieldsOf<T>()` | not started |
| 6 | Migrate the 30 legacy components onto it; generate the schema | not started |
| 7 | `MeshData` skinning + sockets; importer writes `.rmesh` | not started |

Stages 1–4 shipped, and deliberately in that order: `Clip` is the feature that
*proves* D1's claim, so it was built first, against the registry as it stands.
Stage 5 is the refactor that makes the claim structural rather than exemplary —
it deletes the four hand-written restatements per component — and is mechanical
now that a consumer exists to justify it. Stage 7 is independent and can be done
whenever the viewmodel work next needs a socket.

### What stage 4 cost, and why that is the argument for stage 5

Adding `Clip` to the scene format touched exactly the nine files this document
opens by counting: the header, the registry, the schema, `SceneDocument.h`, the
reader, the writer, the cooker, the inspector, and the editor's component table.
Its JSON is hand-written in two of those because a list of tracks of keys is not
a field table — which is real, and is why `Scripts` and `Properties` are too.

But `duration`, `mode`, `speed` and `autoplay` are a field table, and they were
still typed out four times. That is the tax stage 5 removes, measured rather
than asserted.

## 4. What this deliberately does not do

- **No new scene file format.** `.scn` stays JSON, stays version 2, and every
  file on disk keeps loading. The generic path emits the same shapes the hand
  written one does.
- **No change to the rendered image.** Nothing here touches a shader, a
  material, a compositor or a render preset. `make visual-test` is the proof.
- **No second animation runtime.** `Clip` is ~300 lines and cannot blend. If a
  case needs blending it needs ozz, and that boundary is stated in D3 so the
  clip player is not grown into a state machine by accretion.
- **No entity/actor unification.** "Actor" is already derived rather than
  authored (`actorKindOf()`), which is the right shape. It reads as a separate
  standard only because it is one of the 35 unrelated keys, and D1 fixes that by
  fixing the category.
