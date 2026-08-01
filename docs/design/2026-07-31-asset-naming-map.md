# D14 Naming Rename Map (report only, apply nothing)

Companion to `2026-07-31-unified-asset-root.md` §2 D14. This is the mechanical
rename map for `tools/assetlint.py`'s planned naming check. **No renames are
applied by this document** — it is the P6 worklist.

Rule recap:

```
files        lower_snake_case, no spaces, no double extensions
logical ids  path under the domain dir, extension stripped
materials    Pack/Domain/Name in PascalCase   e.g. Game/Kit/Stone, Engine/Particles/Fire
```

Trees scanned: `engine/assets`, `game/assets`, `samples/psx-demo/assets`
(147 + 247 + 65 = 459 files). `hay.jpg`/`jute.jpg`/`market.jpg`/`wood_planks.jpg`
have already been converted to `.png` (D11 landed) — the tables below reflect
that, not the JPEGs the original design doc described.

Excluded from the file tables on purpose: `game/assets/schemas/scene.schema.json`,
`game/assets/templates/membrane.*.in`, `game/assets/templates/effect.template.toml`
— these have a second, dotted extension by design (`.schema.json`, the CMake
`configure_file` `.in` convention) and are not content assets D14 targets.
`game/assets/scenes/tech_demo.map` is a cooked binary, not renamed, only
re-cooked if a path *inside* it changes.

---

## 1. File renames, by domain

### 1.1 `engine/assets`

| Domain | Current | Proposed | Notes |
|---|---|---|---|
| fonts | `fonts/DejaVu-LICENSE` | `fonts/deja_vu_license` | Attribution file, not loaded by path in code. Cosmetic only. |
| fonts | `fonts/DejaVuSansMono.ttf` | `fonts/deja_vu_sans_mono.ttf` | Loaded by literal path in `RenderCore.cpp` — see §4 expensive. |
| particles/textures | `particles/textures/README.md` | `particles/textures/readme.md` | Docs, not an asset. Grep for `README.md` hits third_party headers as text-substring noise, not real references — genuine referrers: none. |
| textures | `textures/EnginePrototypeParticle.png` | `textures/engine_prototype_particle.png` | Referenced by `psx.material` + `PrototypeAssets.h` (literal string). |
| textures | `textures/EnginePrototypeSprite.png` | `textures/engine_prototype_sprite.png` | Referenced by `PrototypeAssets.h` only. |
| textures | `textures/EnginePrototypeSurface.png` | `textures/engine_prototype_surface.png` | Referenced by `psx.material` + `PrototypeAssets.h`. |
| textures | `textures/regenerate-prototypes.sh` | `textures/regenerate_prototypes.sh` | Tooling script sitting inside a content dir, not itself an asset — arguably belongs outside `assets/` entirely per the D3 "raw art leaves assets/" spirit; flagging rather than proposing a mechanical move. |

No collisions inside `engine/assets` after rename (checked flat-namespace for
`textures/` + `particles/textures/` together — Ogre's actual registered
resource dirs per `assets.toml`).

### 1.2 `game/assets/meshes` (46 files, `kit/` sub-domain)

Every `.obj` under `meshes/kit/` is `PascalCase.obj` or `Snake_Case.obj` today; none
collide after lowercasing (verified programmatically — no two kit meshes reduce
to the same basename). Representative sample (full list is mechanical: lowercase
the existing name, no reordering):

| Current | Proposed |
|---|---|
| `meshes/kit/Arch.obj` | `meshes/kit/arch.obj` |
| `meshes/kit/Arch_Fence.obj` | `meshes/kit/arch_fence.obj` |
| `meshes/kit/Arch_Roof.obj` | `meshes/kit/arch_roof.obj` |
| `meshes/kit/Barrel.obj` | `meshes/kit/barrel.obj` |
| `meshes/kit/Block.obj` | `meshes/kit/block.obj` |
| `meshes/kit/Block_Arch.obj` | `meshes/kit/block_arch.obj` |
| `meshes/kit/Block_Half.obj` | `meshes/kit/block_half.obj` |
| `meshes/kit/Box.obj` | `meshes/kit/box.obj` |
| `meshes/kit/Candle_01.obj` | `meshes/kit/candle_01.obj` |
| `meshes/kit/Candle_02.obj` | `meshes/kit/candle_02.obj` |
| `meshes/kit/Candle_03.obj` | `meshes/kit/candle_03.obj` |
| `meshes/kit/Chain.obj` | `meshes/kit/chain.obj` |
| `meshes/kit/Chandelier.obj` | `meshes/kit/chandelier.obj` |
| `meshes/kit/Chest_Base.obj` | `meshes/kit/chest_base.obj` |
| `meshes/kit/Chest_Lid.obj` | `meshes/kit/chest_lid.obj` |
| `meshes/kit/Debris.obj` | `meshes/kit/debris.obj` |
| `meshes/kit/Door_01.obj` | `meshes/kit/door_01.obj` |
| `meshes/kit/Door_02.obj` | `meshes/kit/door_02.obj` |
| `meshes/kit/Door_03.obj` | `meshes/kit/door_03.obj` |
| `meshes/kit/Door_Frame_01.obj` | `meshes/kit/door_frame_01.obj` |
| `meshes/kit/Door_Frame_02.obj` | `meshes/kit/door_frame_02.obj` |
| `meshes/kit/Floor_Tiles.obj` | `meshes/kit/floor_tiles.obj` |
| `meshes/kit/Hexagon.obj` | `meshes/kit/hexagon.obj` |
| `meshes/kit/Pillar.obj` | `meshes/kit/pillar.obj` |
| `meshes/kit/Pillar_Collapsed_01.obj` | `meshes/kit/pillar_collapsed_01.obj` |
| `meshes/kit/Pillar_Collapsed_02.obj` | `meshes/kit/pillar_collapsed_02.obj` |
| `meshes/kit/Skull_Wall.obj` | `meshes/kit/skull_wall.obj` |
| `meshes/kit/Skull_Wall_Half.obj` | `meshes/kit/skull_wall_half.obj` |
| `meshes/kit/Spikes.obj` | `meshes/kit/spikes.obj` |
| `meshes/kit/Stairs.obj` | `meshes/kit/stairs.obj` |
| `meshes/kit/Stairs_Ultrawide.obj` | `meshes/kit/stairs_ultrawide.obj` |
| `meshes/kit/Stairs_Wide.obj` | `meshes/kit/stairs_wide.obj` |
| `meshes/kit/Wall_01.obj` | `meshes/kit/wall_01.obj` |
| `meshes/kit/Wall_02.obj` | `meshes/kit/wall_02.obj` |
| `meshes/kit/Wall_Border_01.obj` | `meshes/kit/wall_border_01.obj` |
| `meshes/kit/Wall_Border_02.obj` | `meshes/kit/wall_border_02.obj` |
| `meshes/kit/Wall_Decor_01.obj` | `meshes/kit/wall_decor_01.obj` |
| `meshes/kit/Wall_Expanding.obj` | `meshes/kit/wall_expanding.obj` |
| `meshes/kit/Wall_Half.obj` | `meshes/kit/wall_half.obj` |
| `meshes/kit/Wall_Narrowing.obj` | `meshes/kit/wall_narrowing.obj` |
| `meshes/kit/Wall_Ruin.obj` | `meshes/kit/wall_ruin.obj` |
| `meshes/kit/Wall_Table.obj` | `meshes/kit/wall_table.obj` |
| `meshes/kit/Windowed_Wall_01.obj` | `meshes/kit/windowed_wall_01.obj` |
| `meshes/kit/Windowed_Wall_02.obj` | `meshes/kit/windowed_wall_02.obj` |

`meshes/props/*.obj` and `meshes/primitives`-equivalent top-level files
(`box.obj`, `crystal_ground.obj`, `crystal_spire{1..4}.obj`, `light_shaft.obj`)
are **already** `lower_snake_case` — no rename. `meshes/bevel-box.obj` violates
on the hyphen only:

| Current | Proposed |
|---|---|
| `meshes/bevel-box.obj` | `meshes/bevel_box.obj` |

### 1.3 `game/assets/textures`

| Domain | Current | Proposed |
|---|---|---|
| top-level | `textures/PINKY.png` | `textures/pinky.png` |
| top-level | `textures/Prototype_orange_32x32px.png` | `textures/prototype_orange_32x32px.png` |
| top-level | `textures/ASSET_LIBRARY.md` | `textures/asset_library.md` (docs, not an asset) |
| top-level | `textures/metal-tex.png` | `textures/metal_tex.png` |
| dungeon | `textures/dungeon/CratesAndBarrels_Map.png` | `textures/dungeon/crates_and_barrels_map.png` |
| dungeon | `textures/dungeon/Doors_Map.png` | `textures/dungeon/doors_map.png` |
| dungeon | `textures/dungeon/Dungeon_Map.png` | `textures/dungeon/dungeon_map.png` |
| dungeon | `textures/dungeon/TEX_Arch_02.png` | `textures/dungeon/tex_arch_02.png` |
| dungeon | `textures/dungeon/TEX_Barrel_01.png` | `textures/dungeon/tex_barrel_01.png` |
| dungeon | `textures/dungeon/TEX_Candle_01.png` | `textures/dungeon/tex_candle_01.png` |
| dungeon | `textures/dungeon/TEX_Chain_02.png` | `textures/dungeon/tex_chain_02.png` |
| dungeon | `textures/dungeon/TEX_Crate_01.png` | `textures/dungeon/tex_crate_01.png` |
| dungeon | `textures/dungeon/TEX_Decor_02.png` | `textures/dungeon/tex_decor_02.png` |
| dungeon | `textures/dungeon/TEX_Door_01.png` | `textures/dungeon/tex_door_01.png` |
| dungeon | `textures/dungeon/TEX_Door_02.png` | `textures/dungeon/tex_door_02.png` |
| dungeon | `textures/dungeon/TEX_Ground_04.png` | `textures/dungeon/tex_ground_04.png` |
| dungeon | `textures/dungeon/TEX_Lock_01.png` | `textures/dungeon/tex_lock_01.png` |
| dungeon | `textures/dungeon/TEX_Metal_01.png` | `textures/dungeon/tex_metal_01.png` |
| dungeon | `textures/dungeon/TEX_Pillar_01.png` | `textures/dungeon/tex_pillar_01.png` |
| dungeon | `textures/dungeon/TEX_Planks_01.png` | `textures/dungeon/tex_planks_01.png` |
| dungeon | `textures/dungeon/TEX_Rusty_Metal_01.png` | `textures/dungeon/tex_rusty_metal_01.png` |
| dungeon | `textures/dungeon/TEX_Skull_Wall.png` | `textures/dungeon/tex_skull_wall.png` |
| dungeon | `textures/dungeon/TEX_Stair_02.png` | `textures/dungeon/tex_stair_02.png` |
| dungeon | `textures/dungeon/TEX_Wall_03.png` | `textures/dungeon/tex_wall_03.png` |
| dungeon | `textures/dungeon/TEX_Water_01.png` | `textures/dungeon/tex_water_01.png` |
| props | `textures/props/MedievalWeaponsSword.png` | `textures/props/medieval_weapons_sword.png` |

`textures/props/{bauerhaus,chest_albedo,generic_wood,hay,jute,lamp_texture,
market_misc,market,terracotta,wood_planks}.png` (recently converted from
JPEG) are already compliant — **not owned by this map**; another agent is
actively converting that directory, so it was read-only for this survey.
`textures/prototype/proto_{dark,green,light,orange,purple,red}_NN.png` (65
files), `textures/surfaces/*`, `textures/vfx/*`, `textures/floor.png`,
`textures/shadow.png`, `textures/sparkle.png`, `textures/white.png` are all
already `lower_snake_case` — no rename.

`game/assets/templates/README.md` → `templates/readme.md`: same "docs, not
an asset" note as §1.1.

### 1.4 `samples/psx-demo/assets` (the duplicate subset)

These are physically separate files today but 58/61 are byte-identical
copies of `game/assets` originals (design doc §1.2) slated for deletion in
Phase P4 (dedupe), not rename. Renaming them is only useful if P4 has not
landed yet by the time D14 executes — recommendation in §5 is to run P4
first so this subset mostly disappears instead of being renamed twice.

| Current | Proposed |
|---|---|
| `meshes/bevel-box.obj` | `meshes/bevel_box.obj` |
| `meshes/kit/Arch.obj` | `meshes/kit/arch.obj` |
| `meshes/kit/Pillar.obj` | `meshes/kit/pillar.obj` |
| `textures/Prototype_orange_32x32px.png` | `textures/prototype_orange_32x32px.png` |
| `textures/dungeon/CratesAndBarrels_Map.png` | `textures/dungeon/crates_and_barrels_map.png` |
| `textures/dungeon/Doors_Map.png` | `textures/dungeon/doors_map.png` |
| `textures/dungeon/Dungeon_Map.png` | `textures/dungeon/dungeon_map.png` |
| `textures/dungeon/TEX_Chain_02.png` | `textures/dungeon/tex_chain_02.png` |
| `textures/dungeon/TEX_Planks_01.png` | `textures/dungeon/tex_planks_01.png` |
| `textures/dungeon/TEX_Wall_03.png` | `textures/dungeon/tex_wall_03.png` |
| `textures/metal-tex.png` | `textures/metal_tex.png` |
| `textures/props/MedievalWeaponsSword.png` | `textures/props/medieval_weapons_sword.png` |

### 1.5 Collisions

**None found.** Checked programmatically: for each pack's flat Ogre resource
namespace (`engine/assets/textures*`, `game/assets/textures*`,
`samples/psx-demo/assets/textures*` — the dirs actually listed under
`resources` in the target `assets.toml`), no two files reduce to the same
lowercased basename after the rename. The one real collision in the whole
system is a **material** name, not a file name — see §1.4 and §2 (`Game/PortalDown`
defined twice with different content, game vs. demo).

Total files renamed: **91** (7 engine, 71 game, 12 samples/psx-demo, with 1
mesh + 8 texture + 1 material-adjacent overlap between game and its demo
duplicates counted once each since they are different physical files).
Of those, **10 are documentation/tooling files** (`README.md` ×3,
`ASSET_LIBRARY.md`, `DejaVu-LICENSE`, `regenerate-prototypes.sh`) that D14's
letter covers but whose rename buys nothing functionally — low priority.

---

## 2. Material renames (`Pack/Domain/Name`)

Pack assignment follows design doc §3 (which pack's `materials/` dir defines
it today, adjusted for the planned pack move — `editor.material` → `editor`
pack, `demo.material`'s showcase-only content → `demo` pack).

### 2.1 Engine pack

| Current | Proposed | Note |
|---|---|---|
| `Sprite/Opaque` | `Engine/Sprite/Opaque` | **Blocked**, see §4 |
| `Sprite/Alpha` | `Engine/Sprite/Alpha` | **Blocked**, see §4 |
| `Sprite/Additive` | `Engine/Sprite/Additive` | **Blocked**, see §4 |
| `Sprite/Overlay` | `Engine/Sprite/Overlay` | **Blocked**, see §4 |
| `Decals/Alpha` | `Engine/Decals/Alpha` | C++ ref, see §4 |
| `Decals/Additive` | `Engine/Decals/Additive` | C++ ref, see §4 |
| `Engine/PrototypePortal` | `Engine/Prototype/Portal` | |
| `Engine/PrototypePortalUp` | `Engine/Prototype/PortalUp` | |
| `Engine/PrototypeLiquid` | `Engine/Prototype/Liquid` | |
| `Engine/PrototypeLava` | `Engine/Prototype/Lava` | |
| `Engine/PrototypeSlime` | `Engine/Prototype/Slime` | |
| `Engine/PrototypeSurface` | `Engine/Prototype/Surface` | |
| `Engine/PrototypeParticle` | `Engine/Prototype/Particle` | |
| `Particles/Sprite/Alpha` | `Engine/Particles/SpriteAlpha` | 4→3 segments, see §4 |
| `Particles/Sprite/Additive` | `Engine/Particles/SpriteAdditive` | 4→3 segments, see §4 |
| `Particles/Voxel/Solid` | `Engine/Particles/VoxelSolid` | 4→3 segments |
| `Particles/Voxel/Additive` | `Engine/Particles/VoxelAdditive` | 4→3 segments |
| `PSX/Lit` | `Engine/Core/Lit` | |
| `PSX/Unlit` | `Engine/Core/Unlit` | |
| `PSX/LitMetal` | `Engine/Core/LitMetal` | |
| `PSX/UnlitMetal` | `Engine/Core/UnlitMetal` | |
| `PSX/LitTransparent` | `Engine/Core/LitTransparent` | |
| `PSX/UnlitTransparent` | `Engine/Core/UnlitTransparent` | |
| `PSX/LitAlphaScissor` | `Engine/Core/LitAlphaScissor` | |
| `PSX/UnlitAlphaScissor` | `Engine/Core/UnlitAlphaScissor` | |
| `PSX/LightVolume` | `Engine/Core/LightVolume` | |
| `PSX/DebugLines` | `Engine/Debug/Lines` | C++ ref, see §4 |
| `PSX/DebugWireframe` | `Engine/Debug/Wireframe` | C++ ref, see §4 |
| `PSX/DitherPost` | `Engine/Post/Dither` | C++ ref, see §4 |
| `PSX/HardwareResolve` | `Engine/Post/HardwareResolve` | C++ ref, see §4 |
| `PSX/PixelStylize` | `Engine/Post/PixelStylize` | C++ ref, see §4 |
| `PSX/BloomBright` | `Engine/Post/BloomBright` | C++ ref, see §4 |
| `PSX/BloomBlurH` | `Engine/Post/BloomBlurH` | |
| `PSX/BloomBlurV` | `Engine/Post/BloomBlurV` | |
| `PSX/BloomComposite` | `Engine/Post/BloomComposite` | C++ ref, see §4 |
| `__Preview/Sprite` | `Engine/Preview/Sprite` (+ `internal = true`) | See §3 |
| `Engine/Particles/Fire` … `Engine/Particles/PortalWisp` (10 names) | **unchanged** | Already `Pack/Domain/Name`-compliant — this is the pattern the whole scheme is copying. |

### 2.2 Editor pack (moves out of `engine/assets/materials/editor.material`)

| Current | Proposed | Note |
|---|---|---|
| `Editor/Checkerboard` | `Editor/Debug/Checkerboard` | C++ ref, see §4 |
| `Editor/CheckerboardDark` | `Editor/Debug/CheckerboardDark` | C++ ref, see §4 |
| `Editor/FireIcon` | `Editor/Debug/FireIcon` | |
| `__Editor/PlacementGhost` | `Editor/Debug/PlacementGhost` (+ `internal = true`) | See §3, C++ ref |

### 2.3 Game pack

| Current | Proposed | Note |
|---|---|---|
| `Kit/Dungeon` | `Game/Kit/Dungeon` | Heavy C++/`.map` ref, see §4 |
| `Kit/DungeonTwoSided` | `Game/Kit/DungeonTwoSided` | `.map` ref, see §4 |
| `Kit/Doors` | `Game/Kit/Doors` | `.map` ref, see §4 |
| `Kit/Containers` | `Game/Kit/Containers` | |
| `Kit/Metal` | `Game/Kit/Metal` | |
| `Kit/Wood` | `Game/Kit/Wood` | |
| `Kit/Stone` | `Game/Kit/Stone` | Heavy C++/`.map` ref — matches the design doc's own example verbatim |
| `Game/PortalDown` | `Game/Vfx/PortalDown` | C++ ref, see §4. **Also the collision fix**: the demo copy of this name (§2.4) gets a different final name. |
| `Game/PortalUp` | `Game/Vfx/PortalUp` | C++ ref |
| `Fantasy/Lava` | `Game/Vfx/Lava` | C++ ref |
| `Fantasy/Water` | `Game/Vfx/Water` | C++ ref |
| `Fantasy/ToxicSlime` | `Game/Vfx/ToxicSlime` | C++ ref |
| `Fantasy/FelEnergy` | `Game/Vfx/FelEnergy` | |
| `Fantasy/ArcaneGlow` | `Game/Vfx/ArcaneGlow` | |
| `Fantasy/CarvedStone` | `Game/Surfaces/CarvedStone` | C++ ref |
| `Fantasy/WarmDungeonStone` | `Game/Surfaces/WarmDungeonStone` | C++ ref |
| `Fantasy/DarkIron` | `Game/Surfaces/DarkIron` | C++ ref |
| `Fantasy/AgedWood` | `Game/Surfaces/AgedWood` | C++ ref |
| `Game/SpellMuzzle` | `Game/Spells/Muzzle` | |
| `Game/FireballTrail` | `Game/Spells/FireballTrail` | C++ ref (`Spells.cpp`) |
| `Game/FireballImpact` | `Game/Spells/FireballImpact` | |
| `Game/BeamCore` | `Game/Spells/BeamCore` | C++ ref (`ViewModel.cpp`, `CombatConfig.h`) |
| `Game/BeamImpact` | `Game/Spells/BeamImpact` | |
| `Game/PrototypeFloor` | `Game/Prototype/Floor` | C++ ref (`EnemySystem.cpp`) |
| `Game/ProtoArrow` | `Game/Prototype/Arrow` | |
| `Game/ProtoBolt` | `Game/Prototype/Bolt` | |
| `Game/ProjectileVesper` | `Game/Prototype/ProjectileVesper` | C++ ref (`PlayerWeapons.*`) |
| `Game/ProjectileEidolon` | `Game/Prototype/ProjectileEidolon` | C++ ref |
| `Game/ProjectileTalon` | `Game/Prototype/ProjectileTalon` | C++ ref + test (`PlayerWeaponTests.cpp`) |
| `Game/Room` | `Game/Dungeon/Room` | |
| `Game/DungeonTile` | `Game/Dungeon/Tile` | C++ test ref (`MapSerializerTests.cpp`) |
| `Game/DungeonFloor` | `Game/Dungeon/Floor` | |
| `Game/DungeonCeiling` | `Game/Dungeon/Ceiling` | |
| `Game/DungeonWall` | `Game/Dungeon/Wall` | |
| `Game/DungeonTileTwoSided` | `Game/Dungeon/TileTwoSided` | |
| `Game/PropPlanks` | `Game/Props/Planks` | Heavy C++ ref, see §4 |
| `Game/PropBauerhaus` | `Game/Props/Bauerhaus` | C++ ref |
| `Game/PropWood` | `Game/Props/Wood` | |
| `Game/PropMarket` | `Game/Props/Market` | C++ ref |
| `Game/PropJute` | `Game/Props/Jute` | C++ ref |
| `Game/PropTerracotta` | `Game/Props/Terracotta` | C++ ref |
| `Game/PropLamp` | `Game/Props/Lamp` | |
| `Game/PropWeapon` | `Game/Props/Weapon` | |
| `Game/PropHay` | `Game/Props/Hay` | C++ ref (`Dummy.cpp`) |
| `Game/PropBauerhausTwoSided` | `Game/Props/BauerhausTwoSided` | C++ ref |
| `Game/PropPlanksTwoSided` | `Game/Props/PlanksTwoSided` | C++ ref |
| `Game/PropChest` | `Game/Props/Chest` | C++ ref |
| `Game/PropMarketMisc` | `Game/Props/MarketMisc` | |
| `Game/ViewModelWeapon` | `Game/Viewmodel/Weapon` | C++ ref (`ViewModel.cpp`) |
| `Game/ViewModelVesper` | `Game/Viewmodel/Vesper` | C++ ref (`PlayerWeapons.*`) |
| `Game/ViewModelVesperGlow` | `Game/Viewmodel/VesperGlow` | C++ ref |
| `Game/ViewModelEidolon` | `Game/Viewmodel/Eidolon` | C++ ref |
| `Game/ViewModelEidolonGlow` | `Game/Viewmodel/EidolonGlow` | C++ ref |
| `Game/ViewModelTalon` | `Game/Viewmodel/Talon` | C++ ref |
| `Game/ViewModelTalonGlow` | `Game/Viewmodel/TalonGlow` | C++ ref |
| `Game/Singularity` | `Game/Vfx/Singularity` | |
| `Game/PortalStone` | `Game/Vfx/PortalStone` | |
| `Game/Torch` | `Game/Vfx/Torch` | C++ ref (`DungeonMap.cpp`, `LayoutToScene.cpp`) |
| `Game/FireParticle` | `Game/Vfx/FireParticle` | |
| `Game/AshParticle` | `Game/Vfx/AshParticle` | |
| `Game/SmokeParticle` | `Game/Vfx/SmokeParticle` | |
| `Game/EnemyRed` | `Game/Enemy/Red` | C++ ref (`EnemySystem.cpp`, `EnemyDef.h`) |
| `Game/EnemyRedDark` | `Game/Enemy/RedDark` | |
| `Game/EnemyRedPale` | `Game/Enemy/RedPale` | |
| `Game/EnemyRedElite` | `Game/Enemy/RedElite` | C++ ref |

### 2.4 Demo pack (post-P4 dedupe; the showcase-only materials)

| Current | Proposed | Note |
|---|---|---|
| `PSX/Floor` | `Demo/Showcase/Floor` | |
| `PSX/BoxMetal` | `Demo/Showcase/BoxMetal` | |
| `PSX/BoxLit` | `Demo/Showcase/BoxLit` | |
| `PSX/CrystalSpire` | `Demo/Showcase/CrystalSpire` | C++ ref (`SceneFactory.cpp`, `ViewModel.cpp`) |
| `PSX/CrystalGround` | `Demo/Showcase/CrystalGround` | C++ ref (`SceneFactory.cpp`) |
| `PSX/Sparkle` | `Demo/Showcase/Sparkle` | Referenced from `game/assets/particles.toml` too — the "sparkles" effect's material. **Not** the deleted `PSX/Sparkles` Ogre particle system (JOB 1) — different, still-used name. |
| `PSX/LightShaft` | `Demo/Showcase/LightShaft` | C++ ref (`RenderPalette.cpp`) |
| `PSX/CrystalSpirePink` | `Demo/Showcase/CrystalSpirePink` | demo-only |
| `PSX/CrystalGroundPink` | `Demo/Showcase/CrystalGroundPink` | demo-only |
| `PSX/ShowcaseStone` | `Demo/Showcase/ShowcaseStone` | demo-only |
| `PSX/PortalBacking` | `Demo/Showcase/PortalBacking` | demo-only |
| `Game/PortalDown` (demo's redefinition in `samples/psx-demo/assets/materials/kit.material`) | `Demo/Vfx/PortalDown` | **This is the collision fix.** Design doc §1.2/§5-P4 already calls for this rename to resolve the one true name collision — D14 just gives it its final `Pack/Domain/Name` form instead of the doc's placeholder `Demo/PortalDown`. |

Total materials renamed: **~90** of ~121 total (per `assetlint.py`'s own
count: "121 materials" for the `game` mount set, "86" for `psx-demo`, with
overlap). ~30 are already compliant (`Engine/Particles/*` ×10, all Sprite-
prototype-atlas ones etc. — no change).

---

## 3. The `__` prefix: where it is interpreted, and what D14 removes

Exact quote, `engine/src/render/Renderer.cpp:526-537` (`Renderer::materialNames()`):

```cpp
std::vector<std::string> Renderer::materialNames() const
{
    std::vector<std::string> out;
    auto& mm = Ogre::MaterialManager::getSingleton();
    auto it = mm.getResourceIterator();
    while (it.hasMoreElements()) {
        const Ogre::ResourcePtr resource = it.getNext();
        const std::string& n = resource->getName();
        if (n.empty()) continue;
        // Filter engine/Ogre internals + generated helper materials.
        if (n.rfind("Ogre/", 0) == 0) continue;
        if (n.rfind("__", 0) == 0) continue;                 // preview/internal
        if (n.rfind("BaseWhite", 0) == 0) continue;
        if (n.rfind("Sprite/", 0) == 0) continue;            // per-clip generated
        if (n.find("DebugWireframe") != std::string::npos) continue;
        ...
```

`n.rfind("__", 0) == 0` is the entire mechanism: any material whose name
starts with `__` is dropped from the list `materialNames()` returns. That
list is what feeds the editor's material picker (`game/editor/EditorApp.cpp:2414,2451`
→ `mMaterialNames`) and the debug material swatch
(`engine/src/debug/DebugTools.cpp:739` → `s.materialList`). It is **purely a
UI-visibility filter** — the materials still exist in Ogre's `MaterialManager`,
still get warmed up and shader-checked (`Warmup.cpp`'s `unshadedPassCheck`
does not skip `__`-prefixed names, only `Ogre/`, `BaseWhite`, `DefaultSettings`),
and are still bindable by exact name from C++ or a material's `copy`/`clone`
directive.

Consuming code:
- `engine/src/render/MaterialPreview.cpp:102` — `mm.getByName("__Preview/Sprite")`,
  the literal string used to fetch the base sprite-preview material.
- `game/editor/PreviewBridge.cpp:178` — `"__Editor/PlacementGhost"`, passed
  to bind the ghost-placement material while dragging a kit piece in the
  editor.
- `engine/tests/VfxShaderAssetTests.cpp:221` — asserts the material named
  `"__Editor/PlacementGhost"` exists and has the expected fragment program ref.

**What D14 breaks if the prefix is just stripped without the manifest flag:**
renaming `__Editor/PlacementGhost` → `Editor/Debug/PlacementGhost` and
`__Preview/Sprite` → `Engine/Preview/Sprite` makes both pass the `n.rfind("__", 0)`
check and start appearing in `materialNames()` — i.e. in the editor's material
picker and the debug swatch list, where a user could select and assign them
to real geometry. Neither is meant to be user-selectable: the placement ghost
is a translucent drag-preview material and the preview sprite is a staging
material for the material-preview rig itself. D14's plan (§2 in the parent
doc) is to replace the string-prefix check with a boolean `internal` field
read from `assets/assets.toml`'s `[materials] internal = [...]` list — that
list is exactly these two names post-rename. The three call sites above
(`MaterialPreview.cpp`, `PreviewBridge.cpp`, `VfxShaderAssetTests.cpp`) are
the string literals that need updating in lock-step with the rename **and**
`Renderer::materialNames()` needs the new `internal`-flag check added before
the rename can land — otherwise there is a one-commit window where both
materials leak into pickers.

Also worth carrying forward: the `Sprite/` prefix (line 539, `// per-clip
generated`) is a second, undocumented instance of the exact same pattern —
a name prefix doubling as a visibility/behavior switch, plus `Renderer.cpp:711-714`
hardcoding `"Sprite/Alpha"` / `"Sprite/Additive"` / `"Sprite/Overlay"` /
`"Sprite/Opaque"` as base-material names it clones per sprite clip. D14's
material table (§2.1) proposes `Engine/Sprite/*` for these, but that rename
is **blocked**, not just expensive — it requires updating both the filter and
the four hardcoded base-name strings in the same commit, or the renamed
materials disappear from cloning (`Renderer::spawnSprite` or equivalent looks
up `"Sprite/Alpha"` by exact string and would get nothing back). Flagging
this so the owner doesn't discover it mid-rename.

---

## 4. Reference-site counts and the expensive set

Counted via `grep -rl` (literal string match) across `*.toml *.material *.scn
*.cpp *.h`, plus `strings` on `game/assets/scenes/tech_demo.map` (the only
cooked map with a KitCatalog-derived room in it). `ritual_boss_showroom.scn`
is JSON text, already covered by the `.scn` grep.

### 4.1 Files — reference counts (selected; full mesh/texture list follows the same method)

| Rename | Referencing files | Count |
|---|---|---|
| `fonts/DejaVuSansMono.ttf` | `engine/src/render/RenderCore.cpp` (literal path) | 1 — **C++, expensive** |
| `textures/EnginePrototypeParticle.png` | `psx.material`, `PrototypeAssets.h` | 2 — **C++** |
| `textures/EnginePrototypeSprite.png` | `PrototypeAssets.h` | 1 — **C++** |
| `textures/EnginePrototypeSurface.png` | `psx.material`, `PrototypeAssets.h` | 2 — **C++** |
| `meshes/kit/Door_01.obj` | `kit.toml` | 1 (+1 `.map`) — **expensive, `.map` re-cook** |
| `meshes/kit/Floor_Tiles.obj` | `boss_arena_features.toml`, `kit.toml`, `DungeonMap.cpp`, `LayoutToScene.cpp` | 4 (+1 `.map`) — **C++ and `.map`, expensive** |
| `meshes/kit/Pillar.obj` | `ShowcaseScene.cpp`, `kit.toml`, `SceneFactory.cpp`, `DungeonMap.cpp` | 4 (+1 `.map`) — **C++ and `.map`, expensive** |
| `meshes/kit/Wall_01.obj` | `kit.toml`, `DungeonMap.cpp`, `LayoutToScene.cpp`, `KitCatalogTests.cpp`, `LayoutToSceneTests.cpp`, `KitCatalog.h` | 6 (+1 `.map`) — **heaviest single rename, C++ + tests + `.map`** |
| `meshes/kit/Spikes.obj` | `boss_arena_features.toml`, `kit.toml` | 2 (+1 `.map`) — **`.map`** |
| `meshes/kit/Door_Frame_01.obj` | `kit.toml`, `DungeonMap.cpp`, `LayoutToScene.cpp`, `LayoutToSceneTests.cpp` | 4 — **C++, no `.map` hit** |
| every other `meshes/kit/*.obj` | `kit.toml` only | 1 — cheap, TOML-only |
| `textures/dungeon/Dungeon_Map.png` | `kit.material` ×2 (game+demo copies), `game.material` | 3 — TOML/material-only |
| `textures/dungeon/TEX_Wall_03.png` | `kit.material` ×2, `game.material` | 3 — material-only |
| most `textures/dungeon/TEX_*.png` (13 of 19) | none found | 0 — dead-code/unused textures already, or referenced only through a generated/derived name assetlint doesn't catch as a literal; safe, trivial renames |
| `textures/metal-tex.png` | `fantasy_surfaces.material` ×2, `demo.material` ×2 | 4 — material-only |
| `textures/props/MedievalWeaponsSword.png` | `props.material`, `game.material` | 2 — material-only |

### 4.2 Materials — reference counts (selected; full table in §2)

| Rename | Referencing files (deduped, real hits only) | Count |
|---|---|---|
| `Sprite/Alpha` | `particles.material`, `sprite.material`, `Renderer.cpp`, `ParticleBatch.h`, `ParticleMaterials.cpp` | 5 — **blocked, see §3** |
| `Sprite/Additive` | `particles.material`, `sprite.material`, `Renderer.cpp`, `ParticleMaterials.cpp` | 4 — **blocked** |
| `Kit/Dungeon` | `kit.material` ×2, `kit.toml`, `boss_arena_features.toml`, `tech_demo.scn`, `ShowcaseScene.{h,cpp}`, `MaterialPreview.h`, `DungeonMap.cpp`, `SceneFactory.h`, `LayoutToScene.cpp`, `KitCatalogTests.cpp`, `RoomBuilder.h`, `KitCatalog.h` | 14 (+`.map`) — **heaviest material rename** |
| `Kit/DungeonTwoSided` | `kit.material` ×2, `kit.toml`, `tech_demo.scn`, `DungeonMap.cpp`, `LayoutToScene.cpp`, `RoomBuilder.h` | 7 (+`.map`) — **expensive** |
| `Kit/Doors` | `kit.material` ×2, `kit.toml`, `ShowcaseScene.h` | 4 (+`.map`) |
| `Kit/Stone` | `kit.material` ×2, `kit.toml`, `tech_demo.scn`, `DungeonMap.cpp`, `SceneFactory.h`, `SchemaSyncTests.cpp`, `SceneTemplates.cpp` | 9 (+`.map`) — **expensive** |
| `Game/PortalDown` | `kit.material` (demo, the collision copy), `main.cpp`, `ShowcaseScene.cpp`, `prototype_vfx.material`, `VfxShaderAssetTests.cpp`, `PrototypeAssets.h`, `vfx.material`, `SceneFactory.h`, `DebugOverlay.cpp` | 9 — **C++, expensive** |
| `Game/PropPlanks` | `props.material`, `ShowcaseScene.cpp`, `showroom_props.toml`, `game.material`, `dungeon_props.toml`, `SceneFactory.cpp`, `ViewModel.cpp`, `PropSystem.cpp`, `LayoutToScene.cpp`, `LobbyDressingTests.cpp` | 10 — **expensive** |
| `Fantasy/Lava` / `Water` / `ToxicSlime` | `liquids.material`, `main.cpp`, `ShowcaseScene.cpp`, `prototype_vfx.material`, `VfxShaderAssetTests.cpp`, `vfx.material`, `DebugOverlay.cpp` (+TOML for Lava/Water) | 7–9 each — **expensive** |
| `Game/ProjectileVesper/Eidolon/Talon` | `weapons.toml`, `prototype.material`, `PlayerWeapons.{h,cpp}` (+`PlayerWeaponTests.cpp` for Talon) | 3–4 — **C++** |
| `Game/ViewModel*` (7 names) | `weapons.toml`, `game.material`, `PlayerWeapons.{h,cpp}` or `ViewModel.cpp` | 2–4 each — **C++** |
| `__Editor/PlacementGhost` | `editor.material`, `VfxShaderAssetTests.cpp`, `PreviewBridge.cpp` | 3 — **C++, see §3** |
| `__Preview/Sprite` | `psx.material`, `MaterialPreview.cpp` | 2 — **C++, see §3** |
| `Engine/Particles/Fire` … `PortalWisp` (10) | *(unchanged, no rename — already compliant)* | n/a |
| everything else in §2 with no "C++ ref" note | TOML + `.material` only | 1–6, cheap |

### 4.3 The expensive set (do these last, batched, each gated by `make visual-test` + the affected app's screenshot)

**Requires a `.cpp`/`.h` edit in the same change:**
`Sprite/{Opaque,Alpha,Additive,Overlay}` (blocked, not just expensive — needs
the `materialNames()` filter rewritten too), `Decals/{Alpha,Additive}`,
`Engine/Prototype*` (7), `Particles/Sprite/*`, `Particles/Voxel/*`,
`Editor/Checkerboard*`, `__Editor/PlacementGhost`, `__Preview/Sprite`,
`PSX/Debug*`, `PSX/DitherPost`, `PSX/HardwareResolve`, `PSX/PixelStylize`,
`PSX/BloomBright`, `PSX/BloomComposite`, all of `Kit/*` (7), `Game/PortalDown`,
`Game/PortalUp`, `Fantasy/{Lava,Water,ToxicSlime,CarvedStone,WarmDungeonStone,
DarkIron,AgedWood}`, `Game/FireballTrail`, `Game/BeamCore`, `Game/PrototypeFloor`,
`Game/Projectile{Vesper,Eidolon,Talon}`, `Game/DungeonTile`, `Game/Prop{Planks,
Bauerhaus,Market,Jute,Terracotta,Hay,BauerhausTwoSided,PlanksTwoSided,Chest}`,
`Game/ViewModel*` (7), `Game/Torch`, `Game/Enemy{Red,RedElite}`,
`fonts/DejaVuSansMono.ttf`, all three `EnginePrototype*.png`.

**Requires a `.map` re-cook** (confirmed via `strings tech_demo.map`):
`meshes/kit/{Door_01,Floor_Tiles,Pillar,Spikes,Wall_01}.obj` and materials
`Kit/{Doors,Dungeon,DungeonTwoSided,Stone}`. This is a strict subset of the
mesh/material renames already flagged above as C++-touching — the kit pieces
placed in `tech_demo.map` are exactly the ones `DungeonMap.cpp`/`KitCatalog.h`
reference by name, so the C++ constant and the cooked path always move
together. One re-cook (`make cook SCENE=tech_demo.scn`) covers all of them.

Everything **not** listed above is TOML/`.material`-only: rename the file or
material, `sed` the literal string in the owning `.toml`/`.material`, done —
no rebuild required to validate, `assetlint.py` is the check.

---

## 5. Recommended order

1. **Land P4 (demo dedupe) first, or at least in the same window.** Most of
   §1.4's file list and all of `props.material`/`kit.material`/`liquids.material`/
   `fantasy_surfaces.material` under `samples/psx-demo/assets` are duplicate
   copies scheduled for deletion. Renaming them and then deleting them wastes
   a review pass; deleting them and then having 12 fewer files to rename is
   strictly better. `Game/PortalDown`'s collision fix is naturally sequenced
   here too — D14 supplies the final name (`Demo/Vfx/PortalDown`), P4 supplies
   the mechanism (rename before merge, per the parent doc).

2. **Cheap, independent, do anytime, any order — no C++, no `.map`:**
   - Every `meshes/kit/*.obj` except the 5 in the `.map` (§4.3) — TOML-only.
   - Every `textures/dungeon/TEX_*.png` and `*_Map.png` — material-only.
   - `textures/props/MedievalWeaponsSword.png`, `textures/metal-tex.png`,
     `textures/PINKY.png`, `textures/Prototype_orange_32x32px.png`.
   - All materials in §2 with no "C++ ref" note (roughly half the table):
     `Kit/Containers`, `Kit/Metal`, `Kit/Wood`, `Fantasy/FelEnergy`,
     `Fantasy/ArcaneGlow`, `Game/SpellMuzzle`, `Game/FireballImpact`,
     `Game/BeamImpact`, `Game/ProtoArrow`, `Game/ProtoBolt`, `Game/Room`,
     `Game/DungeonFloor/Ceiling/Wall/TileTwoSided`, `Game/PropWood`,
     `Game/PropLamp`, `Game/PropWeapon`, `Game/PropMarketMisc`,
     `Game/Singularity`, `Game/PortalStone`, `Game/FireParticle/Ash/Smoke`,
     `Game/EnemyRedDark`, `Game/EnemyRedPale`.
   These can all land in one PR gated by `assetlint.py` + `make visual-test`
   alone — no build-affecting change, so no rebuild risk.

3. **Documentation/tooling files** (§1.1, §1.3's `README.md` ×3,
   `ASSET_LIBRARY.md`, `DejaVu-LICENSE`, `regenerate-prototypes.sh`) —
   independent of everything, zero functional risk, but also zero urgency.
   Bundle with whichever PR is convenient; not worth its own pass.

4. **The `__` → `internal` flag migration (§3) as its own PR**, gated on
   `assets/assets.toml` existing (P0 of the parent doc) since it needs
   somewhere to put `[materials] internal = [...]`. This must include, in one
   commit: the `Renderer::materialNames()` filter rewrite, the rename of
   `__Editor/PlacementGhost` and `__Preview/Sprite`, and the three consuming
   literal-string updates (`MaterialPreview.cpp`, `PreviewBridge.cpp`,
   `VfxShaderAssetTests.cpp`). Do this before the `Sprite/` rename below,
   since it establishes the pattern (manifest flag replacing name-prefix
   convention) the `Sprite/` fix should reuse rather than duplicate.

5. **`Sprite/*` rename** (blocked item from §3) — bundle the four material
   renames with the `Renderer.cpp` hardcoded-string update and the
   `materialNames()` `Sprite/` filter removal (superseded by whatever
   `internal`/generated-material marking scheme step 4 introduced) in one
   commit. Highest blast radius of any single rename in this map (touches
   sprite billboarding for every particle/decal effect in the game) —
   land it alone, gate on `make visual-test` + a full particle-heavy
   screenshot (`make screenshot` on a scene using `spawnParticles` heavily,
   e.g. `boss_arena_features`).

6. **The `Kit/*` and kit-mesh batch** (§4.3's `.map`-touching set) last,
   together, in one PR: the 7 `Kit/*` materials, the 5 map-referenced
   meshes, the `DungeonMap.cpp`/`KitCatalog.h`/`RoomBuilder.h`/`SceneFactory.h`
   string updates, and the `tech_demo.map` re-cook
   (`make cook SCENE=tech_demo.scn`, then `make cook SCENE=tech_demo.scn VALIDATE=1`
   to confirm). This is the only rename in the whole map that forces a
   re-cook, so it should be the last thing touched — every other phase
   should be verified stable first so a re-cook diff isn't hiding an
   unrelated regression.

7. Everything remaining in §4.1/§4.2's "C++, expensive" rows that isn't
   already covered by steps 4–6 (`Decals/*`, `Engine/Prototype*`,
   `Particles/Sprite|Voxel/*`, `Editor/Checkerboard*`, `PSX/Debug*`/post-fx
   materials, `Game/PortalDown`/`PortalUp`, `Fantasy/*` surfaces/liquids,
   `Game/Projectile*`, `Game/ViewModel*`, `Game/Prop*`, `Game/Torch`,
   `Game/Enemy{Red,RedElite}`, the three `EnginePrototype*.png` +
   `DejaVuSansMono.ttf`) — batch by owning C++ system (viewmodel/weapons,
   props, enemies, prototype-assets, post-processing) the same way the
   parent doc's P8 batches `loadObj` call sites, each batch gated by a
   screenshot of the affected app.
