# Asset And Entity Naming {#doc-asset-naming}

This standard separates machine identity from presentation:

- **Stable ID:** exact ASCII lookup key. Persisted, never localized.
- **Display label:** authored human text. May contain spaces and Unicode.
- **Runtime path:** exact pack-relative file path.

Runtime lookup remains exact and case-sensitive. Loaders must not silently
normalize old references. Producers canonicalize new names; validators report
legacy names; migrations update every reference atomically.

## Core Grammar

| Name kind | Grammar | Example |
|---|---|---|
| Local ID | lowercase snake case | `hollow_soldier` |
| Qualified ID | dot-separated local IDs | `game.particle.fireball_trail` |
| Runtime path | lowercase snake directories/file, lowercase extension | `textures/vfx/flame_01.png` |
| Material ID | exactly `Owner/Domain/Name`, PascalCase | `Game/Kit/DungeonTwoSided` |
| Display label | explicit human text | `Hollow Soldier` |

Allowed stable-ID characters are ASCII `a-z`, `0-9`, underscore, and the
separator belonging to that ID kind. IDs begin with a letter. Empty segments,
double separators, trailing separators, spaces, hyphens, and case-only variants
are invalid.

## By Asset Type

| Type | Stable form | File/path form | Example |
|---|---|---|---|
| Scene | `scene.<semantic_name>` | `scenes/<semantic_name>.scn` | `scene.sunken_archive` |
| Entity | scene-local `<kind>_<meaning>_<NNNN>` | stored in scene | `light_altar_0001` |
| Prefab | `kit.<family>_<variant>` | `meshes/kit/<family>_<variant>.obj` | `kit.wall_ruin` |
| Mesh | path identity | `meshes/<domain>/<subject>[_part_NN][_lod_N].obj` | `meshes/props/raven_arm_part_02_lod_1.obj` |
| Material | `Owner/Domain/Name` | `materials/<domain>.mat` | `Game/Props/RavenArmour` |
| Texture | path identity | `textures/<domain>/<subject>_<map>[_NN].png` | `textures/props/raven_armour_albedo.png` |
| Sprite sheet | qualified registry ID | `textures/sprites/<subject>_<action>[_NN].png` | `game.sprite.fire_burst` |
| Particle effect | qualified registry ID | data in `particles.toml` | `game.particle.fireball_impact` |
| Particle texture | qualified registry ID | `textures/particles/<subject>[_NN].png` | `game.particle_texture.ember_01` |
| Shader | path identity | `shaders/<purpose>.<vert|frag>` | `shaders/particle_sprite.vert` |
| Shader include | path identity | `shaders/<purpose>_common.glsl` | `shaders/surface_common.glsl` |
| Palette | typed local ID | data in `palettes.toml` | `sunken_archive` |
| Enemy | typed local ID | data in `enemies.toml` | `hollow_soldier` |
| Weapon | typed local ID | data in `weapons.toml` | `vesper_needle` |
| Sound event | qualified registry ID | `audio/<group>/<subject>_<action>[_NN].ogg` | `game.sfx.rifle_fire` |
| UI hint | qualified registry ID | data in `hints.toml` | `editor.undo` |

Typed config fields may store a local ID when field type makes registry
unambiguous. Shared registries require qualified IDs. Particle names need
qualification because built-in engine effects and game-authored effects enter
same renderer table.

## Composition Rules

### Semantic Before Numeric

Use meaning where meaning exists:

- `door_open`, `door_locked`, `pillar_broken`
- not `door_alt`, `pillar_new`, `wall_final`

Use two-digit content variants: `_01`, `_02`. Use four-digit scene entity
counters: `_0001`. Counters distinguish instances; they do not explain content.

### Parts And LODs

Part suffix is always `_part_NN`, including one-part imported models. Prefer a
stable sanitized source-node name when available; numeric part index is fallback.
LOD suffix comes last: `_lod_0`, `_lod_1`.

```text
raven_armour_part_02.obj
raven_armour_part_02_lod_1.obj
```

Array position alone must not define long-lived identity. Importer should refuse
a canonical-name collision or append a short lowercase source hash such as
`_h3cb178d4`.

### Texture Maps

Use role suffixes rather than source-tool terminology:

- `_albedo`
- `_normal`
- `_emissive`
- `_mask`
- `_height`
- `_roughness`
- `_metallic`

Atlas and sheet names describe content: `portal_fel_atlas.png`,
`shade_starburst_sheet.png`.

### Materials

Material identity always has three PascalCase segments:

```text
Engine/Psx/Lit
Engine/Particles/Fire
Game/Kit/Dungeon
Game/ViewModels/VesperNeedle
Demo/Showcase/CrystalGround
```

Owner is `Engine`, `Game`, or an explicit tool/sample owner such as `Demo`.
Domain describes use, not file location. Name describes surface and variant.

### Protected Mysteries

Player-visible filenames, scene IDs, loading text, achievements, and map IDs
must not encode total depth count. Use semantic region IDs such as
`scene.sunken_archive`, never `floor_03_of_20`.

## Prohibited Names

Do not introduce:

- `final`, `new`, `copy`, `temp`, `image0`, or unexplained `alt`
- `_p0` part suffixes
- unpadded ordinals such as `door1`
- names beginning with digits
- spaces, hyphens, backslashes, absolute paths, or `..` in runtime assets
- IDs differing only by case
- generated state under `assets/`
- stable IDs derived only from array order

Raw vendor/source art under `assets/source/` may retain original spelling. Any
runtime derivative produced from it must follow this standard.

## Editor Display

Asset Browser shows friendly labels derived from stable IDs while retaining full
ID in tooltip and ImGui identity. Example:

```text
game.particle.fireball_impact -> Fireball Impact
Game/Kit/DungeonTwoSided     -> Dungeon Two Sided
kit.wall_window_wide         -> Wall Window Wide
```

Explicit display label wins. Friendly fallback is never written into content.

## Validation And Migration

Shared API lives in `eng/assets/AssetName.h`:

- `validateAssetName()` checks grammar by name kind.
- `canonicalToken()` is producer-only source-text normalization.
- `friendlyAssetLabel()` is tool-only presentation fallback.

Migration order:

1. Fix validators and full reference graph first.
2. Make importers/generators emit compliant new names.
3. Report existing debt as warnings.
4. Rename one asset category per atomic migration.
5. Update scenes, configs, materials, C++ literals, tests, and manifests together.
6. Re-cook every affected map.
7. Add versioned aliases only for shipped saves or external user content.
8. Promote naming warnings to CI failures after repository is clean.

Never bulk-rename persisted identifiers without reference migration. Enemy and
spawner IDs already enter saves; prefab, mesh, material, particle, palette, and
gameplay IDs enter scenes or cooked maps.
