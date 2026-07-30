# Content Pipeline Format Research

_Primary-source findings for the proposed authored-assets, map, and scene
pipeline. Researched 2026-07-27. This is input to design; it does not replace
the current binary `.map` runtime format described in [GEDD](GEDD.md)._ 

## Decision summary

- Use JSON only for **human-authored source documents**: `.asset.json`,
  `.material.json`, `.prefab.json`, `.scene.json` (or the requested short
  `.scn`), and an optional imported-map intermediate. Validate each document
  before import, then compile it into the existing fast binary `.map` / cooked
  asset cache for runtime.
- Put `format` and an integer `version` in every engine-owned source document.
  A loader accepts only explicitly supported format/version pairs, migrates
  older versions in a named step, and rejects newer versions with a useful
  diagnostic. Do not use a filesystem path or display name as an identity.
- Treat Tiled as a **2D layout and encounter-authoring input**, rather than the
  engine's runtime scene format. Import a constrained `.tmj` profile into the
  same engine scene IR as `.scn`; do not make gameplay depend on raw Tiled IDs.
- Standardise DCC interchange on glTF 2.0 (`.glb` preferred for a single,
  self-contained deliverable; `.gltf` allowed while authoring). Import it into
  engine mesh, skeleton, animation, texture, and material records; preserve
  source metadata only as provenance, never as runtime semantics.
- Resolve asset references through stable logical IDs plus typed fallback IDs.
  Missing non-critical art must keep a level playable and visibly signal the
  issue; missing assets required for collision, a spawn point, or boss logic
  fail validation/cooking rather than silently changing gameplay.

## Proposed contract boundaries

```
DCC / image editor / audio editor       Tiled
              |                           |
              v                           v
      glTF, PNG, WAV, etc.              .tmj/.tsj
              |                           |
              +----------- import --------+
                          |
                          v
  engine-authored JSON: asset/material/prefab/scene declarations
                          |
                   schema + semantic validation
                          |
                          v
      canonical content IR -> cooked cache / binary `.map` -> ECS + renderer
```

This keeps the engine's established rule that the ECS registry is authoritative
at runtime while providing an inspectable, merge-friendly authoring layer. It
also avoids asking the renderer to interpret DCC or Tiled formats directly.

## JSON Schema: validation and evolution

JSON Schema is suitable for structural validation: its specification defines
schemas as JSON documents which assert constraints on JSON instances, and
supports reusable schema resources and `$ref`. Pin a single dialect in every
schema with `$schema`, give every schema a stable absolute `$id`, and factor
common definitions into `$defs`. Sources: [JSON Schema Core, sections 1, 8 and
9](https://json-schema.org/draft/2020-12/json-schema-core) and [the dialect
declaration reference](https://json-schema.org/understanding-json-schema/reference/schema).

Recommended root envelope:

```json
{
  "$schema": "https://psx-dungeon-crawler.invalid/schema/scene-1.schema.json",
  "format": "psx.scene",
  "version": 1,
  "id": "scene.demo.boss_crypt",
  "entities": []
}
```

Schema validation is necessary but insufficient. Follow it with engine semantic
validation: logical IDs must resolve, transforms must be finite, hierarchy must
be acyclic, component combinations must be legal, and required gameplay
markers must exist exactly as the design requires. JSON Schema's own data model
does not preserve object member order or lexical number formatting, and duplicate
object keys have undefined behavior; therefore never base file hashes, merge
rules, or deterministic IDs on JSON formatting. [Core sections 4.2.1–4.2.2](https://json-schema.org/draft/2020-12/json-schema-core#section-4.2.1)
cover these properties.

Version the **engine document** separately from the JSON Schema draft. A schema
ID proves which validator rules apply; `format` + `version` select the importer
and explicit migration. Keep migrations pure and test each old fixture through
the latest migration. For forward compatibility, reject unknown required fields
in core engine records (`additionalProperties: false`); reserve a namespaced
`extensions` object for intentionally tool-specific data.

## Tiled (`.tmj` / `.tsj`) import profile

Tiled's JSON map format provides tile, object, image, and group layers; tile
data may be native JSON or base64 plus optional compression, and tile references
are global tile IDs (GIDs). Infinite maps use chunks. Objects have incremental
IDs unique within the map and may carry typed custom properties, object classes,
and template references. Sources: [Tiled JSON map format](https://doc.mapeditor.org/en/stable/reference/json-map-format/)
(see layer, chunk, and object sections).

Use that capability in a restricted profile:

| Tiled feature | Engine import meaning | Demo use |
|---|---|---|
| Tile layer `Floor` / `Walls` | grid/layout input, converted to static dungeon shell | boss arena floor, walls, pillars |
| Object layer `Gameplay` | prefab/entity placement through an explicit `prefab_id` property | player start, exit, chest, boss spawn |
| Object layer `Collision` | collision-volume input, not render geometry | arena bounds / blockers |
| Object layer `Lights` | typed light entity | torches, boss altar |
| Object properties | validated, namespaced engine parameters | `psx.prefab_id`, `psx.encounter_id` |

Convert Tiled pixel coordinates to engine metres in one importer setting; flip
the required axis once; record the chosen tile size in the import manifest. Do
not use Tiled `name`, incremental object ID, or GID as a stable gameplay ID:
the specification describes them as authoring-oriented or map-local. Instead,
require a logical `psx.entity_id` only where cross-reference/stable save-game
identity is needed, and generate a deterministic scene-local ID otherwise.

For the vertical FPS demo, Tiled is most useful for top-down arena/blockout and
encounter markers. Hand-authored `.scn` remains the canonical format for 3D
transforms, prefab overrides, audio zones, navigation, scripted boss phases,
and arbitrary ECS components. Both importers should output the same scene IR.

## glTF 2.0 importer rules

glTF is an interchange format, not a gameplay scene format. Its specification
defines assets made from indexed arrays; indices are required to point to
existing array elements, while optional `name` fields are application-facing and
not guaranteed unique. This directly supports importing by structural references
but forbids treating glTF names as IDs. [glTF 2.0, asset and indices/names](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#concepts)

The engine convention currently proposed in the model-import design—metres,
`+Y` up, `-Z` forward—must include a coordinate conversion because glTF specifies
a right-handed system with metres, `+Y` up, and `+Z` forward. Apply the
conversion once during import, to geometry, node transforms, animation tracks,
and normals/tangents, then write the canonical engine data. Source: [glTF 2.0
coordinate system and units](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#coordinate-system-and-units).

Initial importer scope should be deliberately narrow and testable:

- Required: static meshes, node hierarchy, positions/normals/UV0, indexed
  triangles, base-color textures, and material assignment.
- Demo-needed: skeletal meshes and animation clips only if the boss is animated;
  otherwise accept a static boss prototype first.
- Explicitly reject with source location: unknown **required** extensions,
  unsupported primitive modes, malformed accessor bounds, non-finite data, and
  texture encodings the renderer cannot load. The spec says a client should not
  load an asset requiring an unsupported extension and requires checking the
  glTF `version`/`minVersion`. [glTF asset versioning and extensions](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#asset).
- Log-and-ignore optional unsupported extensions only when the resulting asset
  remains semantically safe; preserve an import report so artists can repair it.

Do not support a feature merely because glTF permits it. For example, sparse
accessors exist and need special decoding, so either implement them with tests
or reject them during the initial demo. [glTF sparse accessor requirements](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#sparse-accessors)

## Asset identity, resolution, and fallbacks

Use IDs such as `mesh.enemy.crypt_warden`, `material.prototype.missing`, and
`texture.world.crypt_wall_a` as lower-case, namespaced logical keys. Maintain a
typed `assets.manifest.json` record per ID with:

```json
{
  "id": "mesh.prop.crypt_pillar_a",
  "kind": "mesh",
  "source": "models/props/crypt_pillar_a.glb",
  "import": { "pivot": "bottom_center", "collision": "static_mesh" },
  "fallback_id": "mesh.prototype.debug_cube",
  "tags": ["dungeon", "crypt", "static"]
}
```

The reference stored in scenes/prefabs is `{ "asset": "mesh.prop.crypt_pillar_a" }`,
not a raw path. The resolver owns: ID lookup, asset-kind checking, source/cooked
status, fallback traversal, cycle detection, once-per-ID diagnostics, and a
report of every fallback used. This complements the existing material plan's
default fallback and submesh remapping instead of duplicating it.

Fallbacks must be typed and graded:

| Missing kind | Safe fallback | Must fail instead |
|---|---|---|
| cosmetic mesh/material/texture | loud prototype mesh / magenta checker / prototype material | never required for collision or gameplay readability |
| optional VFX/audio | silent/null effect plus diagnostic | combat telegraph or required interaction sound |
| collider/nav/script/boss prefab | none | always: retain known-safe old cooked data or fail cook/load |

Use content hashes for cache invalidation and provenance, not as author-facing
identity. Asset IDs stay stable across a source-file move, re-export, or texture
replacement, which is what makes "downloaded asset replaces prototype" an
automatic manifest update rather than a scene rewrite.

## Implementation sequence for the playable boss demo

1. Add `content/schemas/` and a command-line validator which emits JSON-pointer
   diagnostics; check all source documents in CI.
2. Add the asset manifest/resolver and prototype fallback package before any
   imported art. Make fallback usage visible in the debug overlay and cook
   report.
3. Implement the glTF static-mesh import path with canonical coordinate/pivot
   conversion, material fallback, and import tests.
4. Define the constrained Tiled profile and import one `boss_crypt.tmj` into
   scene IR; preserve Tiled provenance only for editor round-trip/debugging.
5. Implement `.scn` for scene-only data and compile both sources to the current
   ECS/binary `.map` path. Add a fixture test that loads the cooked boss arena
   with zero non-cosmetic fallback uses.

## Source register

- [JSON Schema 2020-12 Core](https://json-schema.org/draft/2020-12/json-schema-core)
  — schema identifiers, dialects, references, vocabularies, and validation
  model.
- [JSON Schema: dialect and vocabulary declaration](https://json-schema.org/understanding-json-schema/reference/schema)
  — practical `$schema` / `$vocabulary` guidance from the schema project.
- [Tiled JSON Map Format](https://doc.mapeditor.org/en/stable/reference/json-map-format/)
  — map/layer/chunk/object/properties fields and their versioned semantics.
- [Khronos glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
  — normative asset versioning, references, coordinates/units, extensions,
  accessors, meshes, materials, scenes, animations, and skins.
