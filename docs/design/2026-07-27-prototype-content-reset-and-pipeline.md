# Prototype Reset and Demo Content Pipeline

## Outcome

The repository is now a prototype-only game-art baseline. Engine shaders,
programs, compositor assets, and generated PSX fallback textures are retained.
The following game-owned imported content was removed: dungeon/prop/tile/crystal
meshes; dungeon/prop/surface/VFX textures; lobby/dungeon/editor maps and prop
catalogues; and non-prototype material scripts. `box.obj`, `bevel-box.obj`,
`prototype.material`, and the prototype texture library remain.

The next demo is an authored entrance, combat route, and boss arena. It must
run with prototypes first; adding a real asset to the catalog then recooking
replaces every use of its logical ID without editing a scene. This demo replaces
the deleted lobby/showcase startup path: a fresh checkout must reach a walkable
prototype arena without requesting deleted content.

## Formats and standard boundary

There is no broadly adopted `.scn` standard worth inheriting. A read-only GitHub
code search for versioned scene JSON found unrelated data, so `.scn` should be a
small project-owned authoring format rather than a copied convention.

| Purpose | Format | Rule |
| --- | --- | --- |
| 3D source interchange | `.glb` / glTF 2.0 | Import/cook it; do not use it as a game level. |
| Optional 2D layout sketch | Tiled `.tmj` | Import a constrained tile/object subset only. |
| Editable game scene | JSON `.scn` | Owns entities, transforms, prefabs, gameplay placements and lights. |
| Runtime scene | existing binary `.map` | Cook `.scn` into the current versioned ECS map serializer. |
| Asset contracts | JSON `.asset`, `.material`, `.prefab` | Logical IDs, tags, dependencies and fallback policy. |

[JSON Schema](https://json-schema.org/draft/2020-12/json-schema-core) supplies
the standard structure-validation, `$id`, `$ref`, and reusable-definition
mechanisms for the JSON contracts. [glTF](https://www.khronos.org/gltf/) is the
official runtime 3D delivery format. [Tiled's JSON map format](https://doc.mapeditor.org/en/stable/reference/json-map-format/)
is suitable as optional layout input, not the scene authority. See also
[content format research](2026-07-27-content-pipeline-format-research.md).

## Layout and resolver

```text
assets-src/                                # editable/imported sources, never runtime-loaded
  models/{environment,props,actors,weapons}/
  textures/{environment,props,actors,vfx,ui}/
  audio/{music,sfx,voice}/
  scenes/
game/assets/
  catalog/{assets,materials,prefabs}.asset
  schemas/                                 # JSON Schema contracts
  scenes/                                  # .scn sources
  maps/                                    # cooked .map outputs
  cooked/{models,textures,audio}/          # generated runtime files
  shaders/                                 # game-owned effect shaders
  programs/                                # game-owned OGRE program declarations
  materials/                               # generated OGRE material scripts
```

`RenderCore` must register the app `shaders/` and `programs/` directories,
before `initialiseAllResourceGroups`, alongside app materials/textures. Until
that change lands, copied effect templates are intentionally inert: they cannot
load successfully just by being placed in these folders.

Each scene holds typed logical IDs, never filenames or absolute paths:

```json
{
  "$schema": "../schemas/scene.schema.json",
  "format": "psx-dungeon-scene",
  "version": 1,
  "id": "scene.demo.descent",
  "entities": [{
    "id": "boss_gate",
    "prefab": "prefab.world.boss_gate",
    "transform": { "position": [0, 0, -12] }
  }]
}
```

`prefab.world.boss_gate` resolves to mesh and material IDs. The material record
resolves `albedo`, `emissive`, and `normal` slots to texture IDs, then the
resolver selects physical cooked files.

| Request | When authored asset is absent |
| --- | --- |
| mesh | use `primitive.bevel_box` |
| opaque albedo | generated `EnginePrototypeSurface.png` |
| sprite/VFX texture | generated sprite or particle prototype texture |
| material | `Game/PrototypeFloor` |
| optional sound | silent no-op plus warning |
| required scene/prefab/boss definition | fail the cook/load with file and JSON path |

The renderer already guards missing texture bindings in `RenderCore.cpp`; make
a game-side `AssetResolver` the primary owner of that policy. `MapPlay.cpp`
currently logs a missing mesh and returns an invalid handle, so it needs the
primitive fallback before accepting real `.scn` scenes.

`AssetResolver` owns typed ID lookup, kind checking, fallback traversal/cycle
detection, once-per-ID diagnostics, and a cook report of every fallback used.
Persist `AssetRef { id }` in new cooked maps, alongside the existing
`MeshSource.path` only during one-release compatibility reading. Raw paths must
never be silently reinterpreted as IDs. Keep explicit named migrations for each
binary map version after that change.

## Workflow

```text
Blender -> .glb  ─┐
image/audio      ├-> importer -> catalog + cooked files -> material generator
Tiled .tmj       ┘                                  │
                                                    v
author .scn -> schema + semantic validation -> prefab expansion -> .map
                                                                  │
                                                                  v
                                                      MapRuntime / ECS / renderer
```

Schema validation checks document shape and version. Semantic validation checks
that every ID resolves or has an allowed fallback; all transforms are finite;
entity IDs are unique; there is exactly one player spawn; and a boss demo has a
completion path. `.scn` must not embed vertices, shader source, generated
material scripts, or physical asset paths.

Prefab expansion must produce deterministic entity order and IDs before the map
writer runs. This makes cooks reproducible and keeps save references, source
diffs, and deterministic tests stable.

## Future placement-editor consumer

Build the future ImGui + ImGuizmo placement editor as a **separate engine
consumer application**, not as a mode embedded in the game executable and not
as a renderer-owned editor. Its canonical save format is `.scn`; it shares the
same catalog, schemas, scene IR, `AssetResolver`, prefab expansion, and
`scene_cook` library as the game build pipeline.

```text
                        game/content library
                   (catalog + schemas + scene IR + cook)
                              /              \
                             v                v
game executable <- cooked .map          scene_editor executable
                                               |
                                  ImGui panels + ImGuizmo viewport
                                               |
                                      edit/load/save canonical .scn
                                               |
                                         invoke scene_cook -> .map
```

### Why `.scn` is the editor save format

`.scn` is JSON, diffable, mergeable, schema-validated, and can preserve
prefab/asset IDs and author-facing names. `.map` is deliberately binary and
registry-shaped for fast runtime loading. Therefore:

- **Commit and edit** `.scn`, catalog, and prefab source documents.
- **Generate** `.map` from the shared cooker; it is a derived cache/build
  output and must never become the editor's authoritative save file.
- The editor may open a `.map` in a read-only inspector/debug mode, but it does
  not save it back. To modify it, recover/import into `.scn` explicitly, then
  cook a new map.
- A preview map may be written to a temporary/cooked location. The editor
  displays a stale-cook badge whenever its in-memory scene differs from the
  latest cooked `.map`.

This decision avoids two incompatible editing paths and ensures a scene made by
hand, an editor, a Tiled importer, or a future external tool enters the same
validation and cooking path.

### Application boundary and responsibilities

| Layer | Owns | Must not own |
| --- | --- | --- |
| `eng` | window/input/render/physics/ECS view APIs | game asset IDs, prefab semantics, scene JSON |
| shared game-content library | `.scn` load/save, scene IR, catalog/resolver, schema/semantic validation, cook | ImGui widgets, OGRE direct calls |
| `scene_editor` app | ImGui docking/panels, ImGuizmo selection/manipulation, undo/redo commands, editor viewport and preview launch | a second serializer or a second asset resolver |
| game executable | load cooked map, gameplay systems, playtest runtime | authoring UI or JSON-to-map cooking |

The editor uses only `eng` public APIs and the shared game-content API; it must
not include OGRE/Jolt internals. Its viewport contains an in-memory expanded
scene IR mirrored into `eng::ecs::Scene` for display. The game remains the
runtime authority; the editor is a source-authoring consumer.

### MVP placement workflow

1. Create/open `scene.demo.descent.scn`; load and validate it through the shared
   content library.
2. Browse typed assets/prefabs from the catalog; drag a prefab into the
   viewport to create an entity with a stable author ID.
3. Select, duplicate, delete, parent, and transform entities through ImGuizmo.
   Snap settings (translation, rotation, scale) live in editor preferences, not
   in a scene unless a level explicitly needs grid rules.
4. Apply edits as reversible commands to the scene IR. Undo/redo changes only
   author data; no renderer or physics handle is stored in history.
5. Save `.scn` atomically after schema/semantic validation. Invalid content
   remains editable but cannot replace the last valid file or launch a preview.
6. Run the same `scene_cook` used by CI; show diagnostics by source location
   and logical ID. Launch the cooked map in the game for a playtest.

The initial editor does not need terrain sculpting, custom scripting, or a
binary-map round trip. It needs reliable placement, visible fallbacks, light
placement, collision volumes, player/boss/exit markers, and a one-click
prototype playtest.

### Editor-specific data and safety

Store only presentation state such as panel layout, last selection, and camera
bookmarks in a user-local `.editor` sidecar or preferences file. Never put it
in `.scn`, `.map`, saves, or the shared catalog. Scene-owned data remains
deterministic and playable without the editor.

The editor shows the resolver result for every selected asset: requested ID,
resolved ID, physical cooked path, fallback status, and any validation warning.
Fallback rendering is a deliberate authoring signal, not a silent convenience.
It cannot publish/cook a scene where a required boss prefab, collision,
navigation marker, or combat telegraph is unresolved.

### Editor verification

| Test | Acceptance condition |
| --- | --- |
| load/save round trip | canonical `.scn` retains entity IDs, hierarchy, prefab overrides, and transforms |
| command history | transform/place/delete undo and redo do not change unrelated entities |
| gizmo integration | translation/rotation/scale respects configured snaps and writes only scene IR |
| shared cook equivalence | CLI and editor cook identical `.map` bytes from the same source |
| preview smoke test | editor can open the prototype boss scene, cook, and launch it without imported art |
| fallback UX | a missing cosmetic asset is visibly marked; a missing required asset blocks cook |

## First-demo asset checklist

- [ ] `model.environment.demo_entrance` — modular wall/floor/arch kit
- [ ] `model.environment.demo_arena` — arena shell and gate
- [ ] `model.prop.torch`; `texture.prop.torch_albedo`
- [ ] `model.prop.ritual_altar`; `texture.prop.ritual_altar_albedo`
- [ ] `model.actor.demo_boss`, collision proxy, idle/attack/hurt/death clips
- [ ] `texture.actor.demo_boss_albedo` and optional emissive mask
- [ ] player starter weapon model and texture
- [ ] fireball, boss projectile, and impact VFX textures
- [ ] player attack, boss attack, hit and death SFX
- [ ] exploration music, boss music, dungeon ambience
- [ ] boss portrait and health-bar sprites
- [ ] stone, wood, boss, and fire material records
- [ ] boss, torch, and altar prefabs
- [ ] `scene.demo.descent.scn` and cooked `demo_descent.map`
- [ ] Scene launches when every optional visual/audio asset is absent.
- [ ] Replacing one catalog source and recooking changes every instance.
- [ ] Missing required boss/prefab/scene fails with a precise diagnostic.
- [ ] Boss can be reached, defeated, and completes the demo.

## Shader, portal, liquid, and particle recipes

The engine-owned PSX shader stack stays frozen. New visual effects are added as
small game-owned programs and materials that use that stack; they must not alter
`engine/assets/shaders/psx.*`, the compositor, or the renderer's global
post-process. Starter files live in `game/assets/templates/`.

The cooker allocates programs and materials under `Game/Vfx/<effect-id>` and
rejects a collision with an existing OGRE resource. It validates shader source,
program declarations, material program references, declared uniforms, texture
slots, and the resulting material load before any `.scn` can use the effect.
Until deliberate hot reload exists, the supported iteration loop is **cook →
restart → inspect**.

```text
new effect idea
  -> copy a template pair into game/assets/shaders/
  -> declare its programs in game/assets/programs/vfx.program
  -> create a generated/catalogue material in game/assets/materials/
  -> create an effect entry in game/assets/particles.toml (if particles are needed)
  -> attach material/effect ID through a prefab or .scn
```

### Quick choice

| Desired look | Start from | Typical use |
| --- | --- | --- |
| swirling/rimmed doorway | `portal.vert` + `portal.frag` | portals, occult seals, void tears |
| animated flat surface | `liquid.vert` + `liquid.frag` | water, blood, poison, lava variants |
| translucent organic film | `membrane.vert.glsl.in` + `membrane.frag.glsl.in` | fleshy gates, magical barriers, boss shields |
| moving sparks/smoke/wisps | particle TOML template | impacts, aura, trails, ambient VFX |

Use a material parameter for colour, speed, grid, and emission before making a
new shader. A new shader is warranted only when the effect needs a new UV
motion, silhouette, or pixel-palette rule. Keep time stepped (`floor(time *
fps) / fps`), nearest filtering, and a deliberately small pixel grid so the
effect belongs to the PSX look.

### Five-minute membrane effect

1. Copy `membrane.vert.glsl.in`, `membrane.frag.glsl.in`, and
   `membrane.material.in`; give all program/material names the same ID, e.g.
   `Game/MembraneCultist`.
2. Add the program declarations to the game VFX program file, pointing to the
   copied shaders. Keep `worldViewProj` and `time` exactly as in the template.
3. Replace `@ALBEDO_TEXTURE@` with a catalog-resolved texture. If unavailable,
   use `EnginePrototypeSurface.png`; never leave a broken filename.
4. Place the material ID on a primitive box/plane prefab in a `.scn`. Test it
   against the normal PSX post-process and with a missing texture.
5. Optionally copy `effect.template.toml`, name it
   `membrane_cultist_wisps`, and spawn it from the same prefab or boss state.

Portal and liquid variants follow the same process: copy the closest existing
shader first, change only the effect-local uniforms, then add a material record.
The current `vfx.program` demonstrates the exact program declarations for
`PortalVS`/`PortalFS`, `LiquidVS`/`LiquidFS`, and `LavaFS`.

### Particle authoring rules

`particles.toml` is already the data-driven effect format. An effect has a
material, lifetime/velocity emitters, optional acceleration, colour ramp, and
size ramp. Start with one emitter and 16--32 particles; increase quota only
after profiling. Use a loop for an attached aura/trail and a burst for impacts.

- Particle materials use `cull_hardware none`, `depth_write off`, and either
  alpha blend (smoke) or additive blend (magic/fire).
- Reuse the engine's `Engine/Particles/*` materials until a real source texture
  or bespoke visual rule is needed; missing VFX art falls back to the generated
  particle diagnostic texture.
- Keep colour and size ramps in the TOML record; do not make a shader solely to
  recolour a particle.
- Add a composition function like `particlefx::spawnFlame` only when several
  named effects must always be spawned together.

The cooker preflights particle data strictly: a nonempty unique effect/material
ID, positive dimensions/quota/lifetimes, finite vectors, ordered ramps within
`[0, 1]`, and a valid blend-family material are mandatory. The current runtime
loader's permissive defaults remain a final guard, not authoring validation.

## Boss demo contract

The prototype scene is a playable vertical slice, not a VFX gallery:

```text
safe entry/readable landmark -> one small combat lesson -> locked threshold
-> boss arena with two telegraphed attacks -> defeat/reward -> clear completion
```

`prefab.enemy.demo_boss` is required and includes collision, health, attack
definitions, VFX/SFX IDs, phase transitions, death/reward event, and arena gate
behavior. It emits named gameplay events—`windup_started`, `strike`,
`projectile_spawned`, `impact`, and `phase_changed`—which drive animation,
damage, VFX, audio, camera feedback, and tests. No consumer duplicates a
wall-clock attack duration.

An optional ambience or music asset may fall back to silence. A missing combat
telegraph must fail the boss-scene cook or select an explicit accessible
replacement. Every gameplay VFX declares one role: telegraph, hit confirmation,
spatial hazard, or ambience; decorative effects cannot obscure enemies,
projectiles, or the exit.

## Implementation review and order

| Current seam | Review | Required change |
| --- | --- | --- |
| `LevelDocument` TOML grid | good procedural/blockout input | retain as generator/importer, not long-term scene source |
| `MapSerializer` `.map` | correct cooked-runtime target | retain; add cook/load fixtures and migration policy |
| `ComponentRegistry` stable IDs | good forward-compatibility base | add component IDs only by reviewed migration |
| `MapRuntime` direct `MeshSource.path` | portability/fallback gap | persist `AssetRef` logical ID and resolve through `AssetResolver`; keep one-release path compatibility |
| material scripts bind filenames | prevents global promotion | generate scripts from `.material` catalog records |
| app shader/program folders unregistered | effect templates cannot load | register both resource locations before OGRE initialisation and test a generated membrane effect |
| permissive particle loader | invalid authoring silently defaults | add a strict cooker preflight validator |
| old lobby/showcase tests/runtime paths | intentionally invalid after reset | replace with prototype `.scn` cook/load fixtures; do not restore art |

`scene_cook` is the only content command used by local development and CI. It
runs schema and semantic validation, imports, resolver/material/program/particle
validation, deterministic prefab expansion, and map writing. Its JSON report
records source hashes, resolved IDs, fallbacks, warnings, and output map version.

Implementation order:

1. Replace the deleted-content startup path with a walkable prototype-only scene fixture.
2. Register app shader/program resource directories and compile one copied effect template.
3. Add catalogs, JSON Schemas, strict particle validation, `AssetResolver`, and resolver unit tests.
4. Add scene IR, deterministic prefab expansion, and `scene_cook` to validate `.scn` and write existing `.map`.
5. Migrate `MapRuntime` to `AssetRef` mesh/material fallback resolution, with one-release raw-path compatibility.
6. Generate OGRE material scripts and validate shader/material/particle records in CI.
7. Add boss prefab/event contracts and build `scene.demo.descent.scn` from prototype IDs.
8. Build `scene_editor` as the second consumer using the shared content library; add placement, ImGuizmo transforms, command history, and CLI-equivalent cook/preview.
9. Replace deleted-content tests with prototype cook/load, boss-event simulation, editor round-trip, and smoke-demo fixtures.
10. Capture representative boss frames, set content budgets, then replace checklist IDs one by one.

The current full content-test gate is expected to fail until step 8 because it
references deliberately deleted lobby/showcase content. Engine compilation and
non-content tests remain useful now.

## Required verification gates

| Gate | Prevents |
| --- | --- |
| schema fixtures | malformed `.scn` and catalog documents |
| semantic fixtures | duplicate IDs, hierarchy cycles, absent spawn/exit, unsupported components |
| resolver fixtures | kind mismatch, fallback cycle, missing required boss assets |
| shader/material fixture | missing source/program/uniform/texture/resource location |
| particle fixture | invalid quota, ramp, TTL, material, or duplicate effect ID |
| cook → map → runtime fixture | source/cooked disagreement and serialization regression |
| prototype-only smoke demo | boot regressions when no imported art exists |
| boss-event simulation | VFX/SFX/damage timing drift and no completion state |

The review rationale and ownership breakdown remain in
[content-pipeline design review](2026-07-27-content-pipeline-design-review.md).
