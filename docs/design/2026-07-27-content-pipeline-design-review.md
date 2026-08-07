# Content Pipeline Design Review {#doc-design-2026-07-27-content-pipeline-design-review}

Reviewed through the repository's architect, technical-design-lead,
technical-artist, tools-programmer, gameplay-programmer, level-designer,
animation-programmer, audio-designer, performance-reviewer, and QA-lead role
guides. Scope: the prototype reset, `.scn`/`.map` content plan, and the first
boss demo.

## Verdict

**Approve the direction; do not implement the asset checklist yet.** The source
scene → cook → binary map boundary is a good fit for the existing EnTT runtime,
and logical IDs are the right mechanism for automatic prototype replacement.
However, three foundation fixes are required before the design can claim a
playable prototype-first workflow.

## Blockers

| Priority | Finding | Evidence | Required decision / change | Owner | Acceptance test |
| --- | --- | --- | --- | --- | --- |
| P0 | Game startup still needs deleted content. | `main.cpp` now loads the authored `showroom.toml`; `LiveLevel.cpp` requests the modular kit, showroom props/exhibits, and demo content. | Make a cooked prototype `.map`/`.scn` demo the sole startup path, or retain this deliberately authored showroom fallback. | gameplay + tools | Fresh checkout launches to a walkable prototype arena with no missing-file warnings. |
| P0 | Game-owned custom shaders/programs are not resource locations. | `RenderCore.cpp` registers app materials/textures/particles but not app `shaders/` or `programs/`. | Register those two app directories before `initialiseAllResourceGroups`; add a resource-init test that compiles one copied membrane template. | engine + technical art | A game-owned shader program and its material load with no missing-program warning. |
| P0 | The proposed catalog/resolver/cooker does not exist. | `MapRuntime` resolves raw `MeshSource.path`; no `.scn` loader or catalog types exist. | Land `AssetResolver` + JSON contract validation before scene authoring. It must resolve typed IDs, detect fallback cycles, report every fallback once, and distinguish cosmetic from required content. | tools + gameplay | A fixture with missing texture/mesh uses the correct prototype; a missing boss prefab fails with JSON path. |

## Architecture and migration review

The existing `.map` serializer is the correct cooked artifact and stable
component IDs are a strong base. Preserve both. Do **not** make `.scn` another
runtime code path; it should compile to a canonical scene IR and then `.map`.

Required contract refinements:

- Choose one extension convention: use requested `.scn` for source scenes and
  `.asset` / `.material` / `.prefab` for JSON contracts. State JSON explicitly
  in every root `format`/`version`; extensions are convenience, not schema.
- Introduce `AssetRef { id }` beside `MeshSource` during migration. New cooked
  maps write IDs; old maps continue to read `MeshSource.path` through a
  one-release compatibility adapter. Do not silently reinterpret a raw path as
  an asset ID.
- Keep binary map version migration explicit. The current reader only rejects a
  newer version; it needs named upgraders for old versions once `AssetRef` is
  persisted.
- A `.scn` prefab expansion must produce deterministic entity order and IDs.
  This matters for source diffs, save references, reproducible cooks, and
  deterministic tests.
- The catalog, shader generator, and cooker are game-side code. The renderer
  receives resolved files/material names only and stays independent of game
  content semantics.

## Technical-art and VFX review

The effect templates are a good low-friction starting point, but need a safe
registration/cook path. Materials and GPU programs are global OGRE resources;
generate names under a reserved game namespace such as `Game/Vfx/<effect>` and
reject name collisions.

- Validate shader source existence, program declarations, material program
  references, uniforms, texture slots, and resulting material load before a
  `.scn` can reference the material.
- Treat the template shader as a visual family, not a generic escape hatch:
  portal, liquid, membrane, and particle effects each have approved blend,
  cull, depth-write, filtering, and stepped-time defaults. New effects should
  derive from one family and override documented parameters first.
- Current startup parses all resources once. Until hot reload is intentionally
  built, the supported iteration loop is **cook → restart → inspect**. Do not
  promise live shader/material reload in author docs yet.
- The particle parser currently defaults malformed/missing fields and skips
  only duplicate or renderer-invalid effects. Add a strict preflight validator:
  nonempty unique ID/material, positive dimensions/quota/lifetimes, ordered
  ramps in `[0, 1]`, finite vectors, valid blend-family material, and a
  diagnostic location for each failure.
- Establish budget measurement before numeric limits. Capture representative
  boss-combat frames and record: live particle count, particle draw calls,
  material/program switches, texture residency, GPU frame time, and fallback
  count. Assign the budget to technical art; the engine supplies the counters.

## Gameplay, level, animation, and audio review

The first scene must prove a boss encounter rather than a visual gallery:

```text
safe entry/readable landmark -> one small combat lesson -> locked threshold
-> boss arena with two telegraphed attacks -> defeat/reward -> clear completion
```

- Model the boss as a required prefab with explicit phase/state data, not as a
  mesh plus an `EnemySpawn` string. Required data: collision, health, attack
  definitions, telegraph VFX/SFX IDs, phase transitions, death/reward event,
  and arena gate behavior.
- Author attack **events** (`windup_started`, `strike`, `projectile_spawned`,
  `impact`, `phase_changed`) as the shared seam for animation, damage, VFX,
  audio, camera feedback, and tests. Do not synchronize any of those by a
  duplicated wall-clock duration.
- A missing optional ambient/music sound may be silent. A missing combat
  telegraph must fail the boss-scene cook or use a deliberate accessible
  replacement; silent danger cues are not acceptable.
- Give every gameplay VFX an explicit role: telegraph, hit confirmation,
  spatial hazard, or ambience. Decorative VFX cannot obscure projectile,
  enemy, or exit readability.

## Tools and QA gate

`scene_cook` should be the one command used by developer machines and CI. It
should run schema validation, semantic validation, imports, material/program
validation, prefab expansion, map writing, and a JSON cook report. The report
contains source hashes, resolved asset IDs, fallback use, warnings, and output
map version.

Minimum automated matrix:

| Test | Failure it prevents |
| --- | --- |
| source schema fixtures | malformed `.scn`/catalog documents |
| semantic fixture set | duplicate IDs, cyclic hierarchy, unsupported components, absent spawn/exit |
| resolver fixtures | kind mismatch, fallback cycle, missing required boss asset |
| shader/material fixture | missing program, texture, uniform or OGRE resource registration |
| particle validator fixtures | invalid quotas, ramps, TTL, material, duplicate effect ID |
| cook → map → runtime fixture | source/cooked disagreement and serialization regression |
| prototype-only smoke demo | a reset that no longer boots without imported art |
| boss event simulation | VFX/SFX/damage timing drift and missing completion state |

## Ordered implementation plan

1. Replace the deleted-content startup path with a prototype-only scene fixture.
2. Register app shader/program resource directories and test one template.
3. Add strict JSON/particle validation and the typed `AssetResolver`.
4. Add scene IR, deterministic prefab expansion, and `scene_cook` to the
   existing `.map` writer.
5. Migrate `MapRuntime` from raw mesh paths to `AssetRef` with compatibility
   reading for old `.map` files.
6. Add boss prefab/event contracts and the first prototype boss encounter.
7. Add measured budgets and content QA gates; then begin replacing prototypes
   from the asset checklist.

No P1 art, music, or shader polish should precede steps 1--5. Those steps are
what make later replacements reliable rather than another manual content pass.
