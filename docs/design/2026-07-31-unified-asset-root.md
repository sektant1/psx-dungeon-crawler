# Unified Asset Root & Asset Contract — Refactor Plan

Status: proposed, 2026-07-31, branch `major-refactor`.

Two halves of one refactor:

- **Part I — one place.** One content root at `assets/`, mounted as ordered
  **packs**, resolved through one engine API. Every app — `game`,
  `scene_editor`, `psx_demo`, the cooker, the tests — looks in the same place
  and asks the same question.
- **Part II — one contract.** The *type* of an asset stops being visible to the
  engine and to the data. `loadObj` becomes `loadMesh`; `mesh = "Barrel.obj"`
  becomes `mesh = "kit/barrel"`; one image format, one particle stack, one
  naming rule. Dropping a `.glb` next to a `.obj` must change nothing above the
  importer.

Related: `ARCHITECTURE.md` (layering), `docs/assets-pipeline.md` (authoring
checklist), `tools/assetlint.py` (content gate).

---

## 1. Current state

### 1.1 Four runtime trees, one of them a copy

| Tree | Files | Size | Registered by |
|---|---:|---:|---|
| `engine/assets` | 147 | 6.8 M | `ENG_ASSET_DIR`, always |
| `game/assets` | 247 | 5.6 M | `APP_ASSET_DIR` for `game`, `scene_editor`, cooker, mapgen, sim |
| `samples/psx-demo/assets` | 66 | 3.5 M | `APP_ASSET_DIR` for `psx_demo` |
| `samples/common/assets` | 1 | 8 K | `DEMO_SCENE_TOML`, compiled into both `psx_demo` and `game` |

Plus root `assets/` — **not** a runtime tree today: raw art drops
(`.zip`/`.rar`/`.blend`), README media (`logo.png`, `preview.gif`,
`preview.mp4`, `psx_portal.webm`), mostly gitignored.

### 1.2 The demo tree is 95 % duplicated game content

62 paths exist in both `game/assets` and `samples/psx-demo/assets`.
**59 are byte-identical.** Only three of those differ, and each difference is
additive, not conflicting:

| File | Delta |
|---|---|
| `materials/demo.material` | demo adds `PSX/CrystalGroundPink`, `PSX/CrystalSpirePink`, `PSX/PortalBacking`, `PSX/ShowcaseStone` |
| `materials/kit.material` | demo redefines `Game/PortalDown` (the game also defines it, elsewhere) — the **only true collision in the repo** |
| `particles.toml` | 30 game effects vs 6 demo-only (`crystal_core`, `crystal_motes`, `hall_dust`, `portal_draw`, `portal_embers`, `treasure_dust`) — **disjoint name sets** |

**Corrected in P4 — a path-level count misses two files.** `materials/liquids.material`
and `materials/props.material` have no same-named counterpart in the game tree,
so they are absent from the table above, but between them they define **14
material names the game also defines** (`Fantasy/{Lava,Water,ToxicSlime}` in
`vfx.material`, `Game/Prop*` in `game.material`). Thirteen of the fourteen are
character-identical bodies; `Game/PropHay` had drifted (demo `PSX_VS_Lit` vs the
game's `PSX_VS_LitPerspective`, from the upgrade in `031b64d`). So the real
collision count blocking the `demo`→`game` mount was **15**, not one, and the
dedupe is a material-name exercise as much as a file-hash one.

So merging is not a conflict-resolution exercise. It is a delete-the-copies
exercise with one material rename.

### 1.3 How paths are found today

- **Compile-time absolute source paths.** ~20 CMake `target_compile_definitions`
  bake `${CMAKE_CURRENT_SOURCE_DIR}/...` into binaries: `ENG_ASSET_DIR`,
  `APP_ASSET_DIR`, `DEMO_SCENE_TOML`, `KIT_TOML`, `ASSET_ROOT`, `RITUAL_SCN`,
  `SCENE_SCHEMA`, `PROJECT_SOURCE_DIR`. The build is not relocatable; there are
  no `install()` rules; `packaging/` only ships a `.desktop` file.
- **String concatenation at every call site.** `mAssets + "/enemies.toml"`,
  `ctx.assets + "/blood.toml"`, `mState.assetRoot + "/scenes/untitled.scn"`,
  and `mState.assetRoot + "/../../playtest.log"` (`EditorApp.cpp:526`) — an
  asset path used to climb back out to the project root.
- **Blind directory walk.** `RenderCore::init` recursively
  `addResourceLocation`s *every* directory under both roots. That registers
  `Promo Materials/`, `Free/Shader Cylinder 64x96 Part 3 Free/` and other
  authoring debris the moment such a folder appears under a root.
- **Per-app uniqueness by accident.** `assetlint.py` documents it: `game` and
  `psx-demo` may both define `PSX/Floor` because Ogre never has both roots
  registered at once. Any naive flat merge breaks this invariant.

### 1.4 What already works and must be reused

- `eng::Content` + `eng::ResourceCache<T>` + `eng::Resource(name, path)` — a
  working typed, cached, reloadable resource layer. It is the seam the "smart
  loading" later hangs off; it needs a path *resolver*, not a replacement.
- **Cooked maps store root-relative paths** (`meshes/kit/Door_01.obj`). As long
  as each pack keeps its domain folders internally, `.map`/`.scn` files stay
  valid and need **no re-cook**.
- `assetlint.py`, `check_layering.py`, `make visual-test`, the warmup guard.

### 1.5 The asset *type* leaks into every layer

Six places where the file format is part of the contract instead of an
implementation detail of the importer:

| Leak | Evidence | Cost of a new format |
|---|---|---|
| **Format-named engine API** | `Renderer::loadObj(path)` — **54 call sites** across 13 files | `loadGltf()` + a branch at every one |
| **Extension baked into data** | 90 `"*.obj"` literals in TOML; `mesh = "Floor_Tiles.obj"` in `kit.toml`; cooked `.map` stores `meshes/kit/Door_01.obj` | re-author every catalog and re-cook every map |
| **Two particle stacks** | engine TOML → `ParticleEffectDesc`, *and* Ogre ParticleFX script `sparkle.particle` (`particle_system PSX/Sparkles`) — **orphaned, referenced by nothing**, yet `Plugin_ParticleFX` is loaded at boot for it | two authoring formats, two sim paths, two determinism stories |
| **Two image formats** | 240 `.png` + 4 `.jpg` (`hay`, `jute`, `market`, `wood_planks`) — lossy, no alpha, and Ogre resolves by flat basename so a `hay.png` would silently shadow `hay.jpg` | per-format sampling/alpha quirks |
| **Import convention in materials** | `ObjLoader` hardcodes `uv.y = 1-v` (Godot's OBJ rule); `kit.material` undoes it in **4+ passes** with `uvScale 1 -1 / uvOffset 0 1` | every new format needs its own counter-flip in every material |
| **Naming has no rule** | meshes `Arch_Fence.obj` / `prop_barrel_p0.obj` / `bevel-box.obj` / `crystal_spire1.obj`; textures `TEX_Wall_03.png` / `Dungeon_Map.png` / `tiny_brick_02.png` / `PINKY.png` / `Prototype_orange_32x32px.png`; materials `PSX/` `Game/` `Kit/` `Fantasy/` `Engine/` `Sprite/` `Editor/` `Decals/` `Particles/` `__Editor/` `__Preview/` | a logical id cannot be derived from a name |

Three authoring languages are in play: TOML everywhere, **JSON** for `.scn`
(with `schemas/scene.schema.json`), binary for cooked `.map`. Unlike the six
above this one is defensible — see D10.

`ModelImport.h` already holds the *right* abstraction (pivot mode, unit scale,
source orientation, bake matrix, cache key). It is format-neutral today and
simply has one implementation. The work is wiring, not design.

---

## 2. Decisions

**D1 — `assets/` is the single runtime content root; packs live inside it.**
Not a flat merge. A flat merge would fuse engine-owned and game-owned content,
which the layering in `ARCHITECTURE.md` exists to keep apart, and would
invalidate every cooked map. Packs give one physical place *and* keep the
`engine → game` dependency direction visible on disk.

**D2 — Mount order = authority order: `editor > game > demo > engine`.**
Per the stated hierarchy. Engine sits at the bottom because it is the base
layer everything else refines. Each app mounts a set:

| App | Mounts (highest first) |
|---|---|
| `scene_editor` | `editor`, `game`, `engine` |
| `game`, `mapgen`, `game_sim`, cooker | `game`, `engine` |
| `psx_demo` | `demo`, `game`, `engine` |

The demo mounting `game` is what deletes 58 duplicate files: it stops carrying
a copy and starts referencing the original.

**D3 — Raw source art leaves `assets/`.** Root `assets/` currently holds
`.zip`/`.rar`/`.blend` inputs and README media. Those move to `art/` (pipeline
inputs, gitignored) and `docs/media/` (README media, committed). `assets/`
ends up containing only what ships.

**D4 — One resolver API in `eng_core`, no compile-time asset macros.**
`eng::assets` owns root discovery, mounts, and `resolve()`. It depends on
`<filesystem>` only, so every layer, every tool and every test can use it
without an upward include.

**D5 — Root discovery at runtime, so builds become relocatable.**
Order: `PSX_ASSET_ROOT` env → `<exe>/../share/psx-dungeon/assets` → `<exe>/assets`
→ build-time `PSX_ASSET_ROOT_DEV` (source dir). The dev default keeps
`make run` working from the build tree; the first three make an installed build
possible for the first time.

**D6 — A manifest declares packs and domains.** `assets/assets.toml` names each
pack and the domain folders inside it. `RenderCore` registers *declared* dirs
instead of walking everything, and `assetlint.py` reads the same file instead of
hardcoding roots. One source of truth for "what content exists".

**D7 — Resolution is per-logical-path, not per-tree.**
`assets::resolve("config/enemies.toml")` walks the mount list. Callers stop
concatenating roots. `assets::resolve("game/config/enemies.toml")` (pack-
qualified) stays available for the cases that must pin a pack.

### Part II — one contract

**D8 — The engine API names the *kind* of asset, never the format.**
`Renderer::loadObj` → `Renderer::loadMesh`. Behind it, a small importer
registry keyed by extension; `ObjLoader` becomes one registered `MeshImporter`.
A future `GltfImporter` is a registration, not an API change. Same rule
everywhere: `loadTexture`, not `loadPng`.

**D9 — Data references a logical id, not a filename.**
`mesh = "Barrel.obj"` → `mesh = "kit/barrel"`. `mesh_dir` in `kit.toml`
disappears; the id *is* the path under the pack's `meshes/`. The resolver picks
the file, preferring the highest-priority mount and then a declared format
order (`.mesh` cooked → `.glb` → `.obj`), so a cooked or upgraded asset drops in
without touching a single catalog. Resolution tolerates a trailing extension
for one release so old `.map` files keep loading — but the maps get re-cooked in
the same phase and the shim is deleted with it.

**D10 — Three tiers of data, one rule each — stated, not ad hoc.**

| Tier | Format | Who writes it | Why |
|---|---|---|---|
| Authored config | **TOML** | humans | comments and diffs; the engine already has one reader |
| Editor document | **JSON + JSON Schema** | `scene_editor` (`.scn`) | machine round-trip; `scene.schema.json` is a real validation gate the editor and cooker both use |
| Cooked runtime | **binary** (`.map`) | `scene_cook` | load speed; never hand-edited |

The lint enforces extension ↔ tier, so nobody adds a fourth. *Not chosen:*
converting `.scn` to TOML — it would delete a working schema gate, touch
`SceneDocument`/`SceneWriter`/`SceneValidate` plus two test files, and buy only
cosmetic uniformity. Revisit only if the editor stops being the sole author.

**D11 — One image format: PNG.** Convert the 4 JPEGs. Lossy compression is
wrong for a nearest-neighbour pixel-art renderer, JPEG has no alpha, and mixed
extensions under Ogre's flat basename lookup is a collision waiting to happen.

**D12 — One particle stack: the engine's.** Delete `sparkle.particle`, stop
loading `Plugin_ParticleFX`, keep TOML → `ParticleEffectDesc`. The script is
already orphaned; the plugin is a boot cost and a second, non-deterministic
(`rand()`-seeded) simulation path for a system the engine otherwise owns.

**D13 — The importer normalizes; materials never compensate.**
The `uv.y = 1-v` flip moves into `ModelImportOptions` (`flipV`, per-asset,
defaulted per format), and `kit.material`'s four `uvScale 1 -1 / uvOffset 0 1`
counter-flips are deleted in the *same commit*. `uvScale`/`uvOffset` go back to
meaning only what their names say. **The image is frozen**: this pair of changes
must be pixel-neutral under `make visual-test`, which is exactly what makes it
safe to do at all.

**D14 — One naming rule, machine-checkable.**

```
files        lower_snake_case, no spaces, no double extensions
             kit/arch_fence.obj   props/prop_barrel_p0.obj   dungeon/wall_03.png
logical ids  the path under the domain dir, extension stripped
             "kit/arch_fence"  "dungeon/wall_03"
materials    Pack / <defining .material file stem> / Name,  PascalCase,
             consecutive duplicate segments collapsed
```

**The material rule is derived, not chosen.** A material's name is its pack,
then the stem of the `.material` file that defines it, then its own name:

| Pack | File | Material | Name |
|---|---|---|---|
| engine | `psx.material` | Lit | `Engine/Psx/Lit` |
| engine | `particles.material` | Fire | `Engine/Particles/Fire` |
| engine | `sprite.material` | Alpha | `Engine/Sprite/Alpha` |
| game | `kit.material` | Stone | `Game/Kit/Stone` |
| game | `vfx.material` | PortalDown | `Game/Vfx/PortalDown` |
| editor | `editor.material` | Checkerboard | `Editor/Checkerboard` (collapsed) |

Why derived rather than hand-picked domains: it takes the judgement out (no
argument over whether `PSX/Lit` should become `Engine/Core/Lit` or
`Engine/Shading/Lit`), it is **machine-checkable** — `assetlint` can assert that
every material's name matches the file it is defined in, so the convention
cannot rot — and it makes the name an index: `Engine/Psx/Lit` says to open
`assets/engine/materials/psx.material`. It also keeps the `PSX` identity, which
`Engine/Core/*` would have erased even though the shaders, programs and
materials are all literally named `psx.*` and the look is the shipped product.

The one cost is that renaming a `.material` file renames every material in it.
That is acceptable: those files are stable, and the lint makes the coupling
explicit rather than surprising.

**Supersedes** `docs/design/2026-07-31-asset-naming-map.md` §2 wherever the two
disagree — the map's per-material target names were picked before this rule
existed. Its inventory, reference counts and ordering all stand.

The `__Editor/` and `__Preview/` prefixes exist only to hide entries from
`Renderer::materialNames()`; they become an `internal` flag in the pack
manifest, so a naming convention stops doubling as a visibility mechanism.
Renames are mechanical and covered by `assetlint`, which gains a naming check —
a violation fails the build instead of accumulating.

---

## 3. Target layout

```
assets/                          # THE content root — everything shipped, nothing else
  assets.toml                    # manifest: packs, mount sets, domain dirs

  engine/                        # pack "engine"  (was engine/assets)
    compositors/  fonts/  materials/  programs/  shaders/  textures/  ui/
    particles/{textures/,textures.toml,sprite_sheets.toml}
    material_preview.toml

  game/                          # pack "game"  (was game/assets + samples/common/assets)
    config/                      # game.toml, enemies.toml, weapons.toml, magic.toml,
                                 # blood.toml, particles.toml, kit.toml, dungeon.toml,
                                 # palettes.toml, prototypes.toml, spawners.toml, ...
    materials/                   # game/kit/vfx/spells/enemy/fantasy_surfaces/prototype
    meshes/{kit/,props/,primitives/}
    textures/{dungeon/,props/,prototype/,surfaces/,vfx/}
    scenes/                      # .scn sources + cooked .map
    schemas/                     # scene.schema.json
    templates/                   # effect.template.toml, membrane.*.in
    showroom/                    # showroom.toml, showroom_props.toml,
                                 # showroom_exhibits.toml, demo_scene.toml

  editor/                        # pack "editor"  (top priority, editor-only)
    config/editor.toml, editor_level.toml
    materials/editor.material    # moved down from engine — nothing in game refs it

  demo/                          # pack "demo"  (deltas only, ~5 files)
    config/demo.toml, particles.toml      # the 6 demo-only effects
    materials/demo.material                # the 4 PSX/* showcase materials
    particles/sparkle.particle

art/                             # pipeline INPUTS, not build inputs (gitignored)
  archives/  blends/  packs/     # was assets/*.zip, *.rar, *.blend, boomer_fps_claude_pack.zip
docs/media/                      # logo.png, logo-transparent.png, misc/logo{1,2,3}.png,
                                 # preview.gif, preview.mp4, psx_portal.webm
```

`game/content/` is **C++** (SceneCook, KitCatalog, RoomBuilder) and does not
move.

### 3.1 `assets/assets.toml`

```toml
# The content manifest. Packs are mounted in the order an app names them;
# the first mount that resolves a logical path wins.
schema = 1

[[pack]]
id = "engine"
dir = "engine"
# Directories registered with Ogre's flat resource group. Anything not listed
# is data the engine reads by path (meshes, configs) and must NOT be a
# resource location -- that is what kept authoring debris out of the group.
resources = ["materials", "programs", "shaders", "compositors", "textures",
             "fonts", "ui", "particles/textures", "particles/textures/sheets"]

[[pack]]
id = "game"
dir = "game"
resources = ["materials", "textures", "textures/dungeon", "textures/props",
             "textures/prototype", "textures/surfaces", "textures/vfx"]

[[pack]]
id = "editor"
dir = "editor"
resources = ["materials"]

[[pack]]
id = "demo"
dir = "demo"
resources = ["materials", "particles"]

# Mount sets, highest priority first. An app names one.
[mounts]
game        = ["game", "engine"]
editor      = ["editor", "game", "engine"]
demo        = ["demo", "game", "engine"]

# D9: how a logical id becomes a file. First hit wins, so a cooked or upgraded
# asset supersedes its source with no data edit.
[formats]
mesh    = [".mesh", ".glb", ".gltf", ".obj"]
texture = [".png"]

# D14: materials hidden from Renderer::materialNames() -- a manifest flag, not
# a "__" name prefix.
[materials]
internal = ["Editor/PlacementGhost", "Preview/Sprite"]
```

---

## 4. Code changes

### 4.1 New — `eng_core`: `eng/assets/AssetRoot.h` + `src/core/AssetRoot.cpp`

```cpp
namespace eng::assets {

// Discovers the content root and parses assets.toml. Called once by
// Engine::init; tools/tests call it directly.
bool init(const std::string& rootOverride = {});

const std::filesystem::path& root();     // .../assets
const std::filesystem::path& project();  // repo root in a dev build; the
                                         // install prefix otherwise. Replaces
                                         // assetRoot + "/../.." in EditorApp.

// Mount a named set from [mounts], e.g. "editor". Later mounts do not
// override earlier ones: the set's own order is the priority.
bool mount(const std::string& mountSet);
const std::vector<Pack>& mounted();

// "config/enemies.toml" -> first mount that has it. Pack-qualified
// ("game/config/enemies.toml") pins one pack. Empty if unresolved.
std::filesystem::path resolve(std::string_view logical);
bool exists(std::string_view logical);

// Every mounted pack dir flagged `resources`, in priority order.
// RenderCore registers exactly this and nothing else.
std::vector<std::filesystem::path> resourceDirs();

} // namespace eng::assets
```

Depends on `<filesystem>` + the existing TOML reader only — clean under
`check_layering.py`.

### 4.2 Changed

| File | Change |
|---|---|
| `eng/app/Application.h` | `AppConfig::assetDir` → `std::string mountSet` (`"game"`, `"editor"`, `"demo"`); `configPath` becomes a logical path (`"config/game.toml"`) |
| `eng/Engine.h`, `src/app/Engine.cpp` | `init()` calls `assets::init()` + `assets::mount()` before `RenderCore::init` |
| `src/render/RenderCore.cpp` | drop `addTree()` walk + `appAssetDir`; register `assets::resourceDirs()`. `ENG_ASSET_DIR "/fonts/..."`, `"/ui/hints.toml"` → `assets::resolve()` |
| `src/render/MaterialPreview.cpp` | delete the `APP_ASSET_DIR/../..` climb (`:176-191`) → `assets::resolve("engine/material_preview.toml")` |
| `src/particles/ParticleMaterials.cpp:216` | → `assets::resolve("engine/particles")` |
| `game/src/main.cpp`, `CombatSystem.cpp`, `SimWorld.cpp`, `Dummy.cpp`, `MapPlay.cpp` | `mAssets + "/x.toml"` → `assets::resolve("config/x.toml")`; drop the `mAssets`/`GameContext::assets` member |
| `game/editor/EditorApp.cpp`, `EditorState.h`, `PreviewBridge.cpp` | `assetRoot` → resolver; `assetRoot + "/../../playtest.log"` → `assets::project() / "playtest.log"` |
| `samples/psx-demo/src/main.cpp`, `ShowcaseScene.cpp` | mount set `"demo"`; `samples/common/DemoScene.cpp` loses `DEMO_SCENE_TOML` |
| `game/tools/scene_cook_main.cpp` | `assetRootFor(kitPath)` heuristic deleted — the cooker mounts `"game"` like everything else |
| `CMakeLists.txt` | delete `ENG_ASSET_DIR`, `APP_ASSET_DIR`, `DEMO_SCENE_TOML`, `KIT_TOML`, `ASSET_ROOT`, `RITUAL_SCN`, `SCENE_SCHEMA` (~20 sites); add one `PSX_ASSET_ROOT_DEV`; add `install()` for `assets/` → `share/psx-dungeon/assets` |
| `engine/tests/*`, `game/tests/*` | `PROJECT_SOURCE_DIR + "/engine/assets/..."` → `assets::resolve("engine/shaders/...")` (9 test files) |
| `tools/assetlint.py` | read `assets/assets.toml`; lint one mount set at a time; also assert no cross-pack collision *within* a set |
| `tools/import_sprite_sheets.py`, `gen_particle_textures.py`, `gen_font_atlas.py`, `regenerate-prototypes.sh` | output paths → `assets/engine/...` |
| `Makefile` | `SCENE=`/`SHOWROOM=` defaults → `assets/game/scenes/...` |
| `.gitignore` | `assets/*.zip` etc. → `art/`; un-ignore nothing under `assets/` |
| `README.md` | media paths → `docs/media/` |
| `ARCHITECTURE.md` | new "Content root" section replacing the per-app-uniqueness paragraph |

### 4.3 New — the mesh importer registry (D8/D9/D13)

`ObjLoader` stops being called by name. It becomes one entry in a registry the
renderer owns:

```cpp
namespace eng {

struct MeshData {                 // what every importer must produce
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec4> colours;   // white when the source has none
    std::vector<uint32_t>  indices;
    std::string            defaultMaterial;  // may be empty
};

// One per format. The engine holds no other knowledge of file types.
class MeshImporter {
public:
    virtual ~MeshImporter() = default;
    virtual std::span<const std::string_view> extensions() const = 0;
    // `options` is already sanitized; the importer applies the bake and the
    // format's own conventions (D13: the V flip belongs HERE, not in a material).
    virtual bool import(const std::filesystem::path&,
                        const ModelImportOptions&, MeshData&) = 0;
};

void registerMeshImporter(std::unique_ptr<MeshImporter>);

} // namespace eng
```

`Renderer` gains the format-neutral entry point, keeping the existing overloads'
shape:

```cpp
// "kit/arch_fence" -- a logical id, not a path, not a filename.
MeshHandle loadMesh(const std::string& id);
MeshHandle loadMesh(const std::string& id, const ModelImportOptions&);
MeshHandle loadMesh(const std::string& id, const glm::mat4* bake);
```

`loadObj` stays as a `[[deprecated]]` forwarder for exactly one phase, so the
54 call sites move in reviewable batches rather than one 13-file commit, then
is deleted. The mesh cache keys off `modelImportCacheKey()`, which already
canonicalises the path and the options — no change needed there.

`ModelImportOptions` gains `bool flipV` (D13) and a per-format default, so the
convention lives with the importer and the materials stop counter-flipping.

### 4.4 Deliberately **not** in scope

Async/streamed loading, pak/archive mounting, GUID asset ids, a content
database, hot-reload beyond what `ResourceCache::reload` already does. D6+D7
are the seams those need; building them now is framework-before-requirement.

---

## 5. Migration phases

Each phase ends green. `git mv` throughout, so history follows the files.

**P0 — Manifest + resolver, zero moves. — DONE (2026-07-31)**
`eng::assets` in `eng_core`, `assets/assets.toml` describing the *current*
trees (`dir = "../engine/assets"` etc.). `RenderCore` was **not** ported —
kept for P1, so P0 stays purely additive.
Gate met: ctest 86 → **87/87**, `check_layering.py` clean, warmup
`133 materials, 0 unsupported`, game screenshot correct under `xvfb-run`.

As-built deviations from §4.1:
- Added `ready()` (distinguishes "nothing mounted" from "never initialised";
  every getter must stay safe pre-init) and `packs()` (assetlint needs to see
  undeclared-but-existing packs in P2+). `init()` is re-callable and resets
  state; `mount()` replaces rather than accumulates.
- **No `editor` pack yet** — `assets/editor/` does not exist until P3 and a
  pack with a missing `dir` fails init by design, so the `editor` mount set is
  the game set for now.
- `[formats]` and `[materials] internal` are declared in the manifest but
  **not parsed** — forward declarations for P6/P9; parsing without exposing
  would be dead code.
- §4.1's claim that the module is "clean under `check_layering.py`" was
  **wrong**: `HEADER_RULES` ends in a catch-all `("", "systems")`, so a new
  top-level header dir classifies as *systems* and makes
  `src/core/AssetRoot.cpp` an upward include. Needs `("assets/", "core")`.
  Any future top-level header directory has the same problem.

**P1 — Engine pack.** Split in two. The **code half is DONE (2026-07-31)**:
`RenderCore` registers `assets::resourceDirs()` instead of walking directories,
`AppConfig::assetDir` → `mountSet`, `Engine::init` mounts, and the
`ENG_ASSET_DIR` uses in `RenderCore`/`MaterialPreview`/`ParticleMaterials` are
resolved. Gate met: **87/87**, all three apps warmup-identical
(`game` 133/53, `psx_demo` 97/42) and screenshot **md5-identical** to the
pre-change tree. The **file move** (`git mv engine/assets assets/engine`, `dir`
→ `engine`) and the 6 asset-reading test files are still pending.

Two findings worth keeping:

- **The mandated old-vs-new directory diff earned its keep.** The blind walk
  registered 25 dirs for the game set; the manifest declared 16. One drop was
  real: `engine/assets/particles/textures/sheets`. `sprite_sheets.toml` names a
  bare filename (`sheet = "bullet16.png"`) and `ParticleMaterials::buildMaterial`
  binds it straight to a texture unit, so Ogre must find it by basename —
  losing that dir would have sent **304 sheet-backed particle textures** to
  PINKY, silently, and only when an effect spawned. §3.1 listed the dir; the
  P0 manifest dropped it. Every other exclusion was correct: dirs holding
  only path-loaded data (meshes, scenes, schemas, config TOMLs) and
  `game/assets/templates`, whose `README.md` was the source of a long-standing
  boot warning that is now gone.
- **`ENG_ASSET_DIR` is not fully dead.** `ImGuiLayout.cpp:96` uses it to
  *write* `ui/debug_layout_<app>.ini`. `resolve()` answers "where is this
  file", not "where may I create one". The macro stays on `eng_systems` for
  now; P2 must either add a `packDir(id)` accessor or decide that ini belongs
  in a user config dir rather than the asset tree.

**Ordering correction to D2.** `demo = ["demo","game","engine"]` **cannot be
applied before P4**. Mounting `game` under the demo while the demo tree is
still a copy aborts `psx_demo` at boot: Ogre *throws*
`ItemIdentityException — Material with the name Fantasy/CarvedStone already
exists` out of `ResourceManager::add` during script parsing (exit 134), it does
not warn. §4.2 scheduled the mount-set switch in P1 and the dedupe in P4; the
switch has to move *with* P4.

**P2 — Game pack.** `git mv game/assets assets/game`, then regroup TOMLs into
`config/` and showroom files into `showroom/`. Absorb
`samples/common/assets/demo_scene.toml` → `assets/game/showroom/`.
`meshes/`, `textures/`, `scenes/` keep their internal shape, so cooked `.map`
files stay valid. Gate: `assetlint`, ctest, `make scene`, editor screenshot.

**P3 — Editor pack.** Split `editor.toml`, `editor_level.toml` and
`editor.material` out of the game/engine packs into `assets/editor/`.
Gate: `make editor`, `make material`.

**P4 — Demo pack, the dedupe. — DONE (2026-07-31)**
59 byte-identical copies deleted; `liquids.material` and `props.material`
deleted whole (see §1.2 — every material in them was a duplicate);
`kit.material` reduced to its one surviving profile and renamed
`portal.material`, its `Game/PortalDown` renamed `Demo/PortalDown`.
`assets.toml`: `demo = ["demo", "game", "engine"]`, demo `resources` pruned to
`["materials"]`. The demo pack is now four files.
Gate met: ctest **87/87**, `assetlint` clean on all three mount sets,
`check_layering` clean, `psx_demo` warmup 97 → **137 materials, 0 unsupported**
and 42 → **52 textures resident**, no `ItemIdentityException`, exit 0. `game`
and `scene_editor` unchanged (133 materials / 53 textures, screenshots
md5-identical).

As-built deviations:
- **`samples/psx-demo/assets` was not deleted**, only emptied of duplicates: it
  still holds `demo.toml`, `particles.toml` and the two material files. It
  becomes `assets/demo/` in a later move, along with `samples/common/assets`
  (P2's, per the phase split).
- The demo loaded its **meshes by path** off `APP_ASSET_DIR`, so deleting its
  mesh copies required porting `ShowcaseScene` to `assets::resolve()` first.
  `assetDir` is gone from `ShowcaseScene::build` and its five helpers, and
  `psx_demo`'s `APP_ASSET_DIR` define is deleted. Textures needed no such port —
  they resolve through Ogre's resource group, which the manifest already feeds.
- `tools/assetlint.py` had **not** been ported in P0/P1 (§4.2 lists it). P4 had
  to do it, because the lint's hardcoded per-app roots do not know the demo
  mounts the game. It now reads `[mounts]` from `assets.toml` and lints one
  mount set at a time — which is also the cross-pack collision check §7 asks
  for, since a duplicate inside a set is exactly what rule 4 already reports.
- `Game/PropHay`: the demo's copy was the drifted one, so deleting it moves the
  showcase haybale onto the game's perspective-correct profile. Visible
  difference at frame 200 and frame 500: **4 pixels each, max channel delta
  32/765**.
- **§7's risk table is one namespace short.** Mounting two packs together
  collides on *file basenames* as well as resource names: the demo's
  `demo.material` and the game's shadowed each other in Ogre's flat file index
  ("Skipping 'demo.material' because it already exists"). Both scripts are still
  parsed, so nothing broke, but `openResource("demo.material")` became
  ambiguous. Renamed `showcase.material`, and `assetlint` rule 5 now covers
  `.material` basenames the way it already covered texture basenames.

**P5 — Source art + media. — DONE (2026-07-31)**
`assets/` now contains exactly `assets.toml`. 134 MB → `art/` (gitignored
wholesale), 14 MB → `docs/media/` (still tracked — verified the `.gitignore`
rewrite did not swallow `preview.mp4`/`preview.gif`). 89 tracked files moved
with `git mv`; the gitignored archives with plain `mv`. Zero C++/CMake
references to any moved path, confirmed by grep.

Two things this turned up:
- `packaging/install-desktop.sh` pointed at `assets/logo1.png`, which never
  existed (the file was `assets/misc/logo1.png`). Pre-existing bug, fixed on
  the way past.
- `tools/import_sprite_sheets.py` hardcoded `SRC = assets/sprites/shaders`,
  which the move deleted. Repointed at `art/sprites/shaders`.
  `engine/assets/particles/sprite_sheets.toml:4` still names the old path in a
  generated comment; it is regenerated by that tool, so it corrects itself on
  the next import.

**P6 — Cheap standardisations.** No API change, so they land early and shrink
every later diff.
- D11 — **DONE**: the 4 JPEGs are PNG in both trees, decoded pixels verified
  identical per file, `.material` references updated, zero JPEGs remain.
- D12 — **DONE**: `sparkle.particle` deleted (verified orphaned: no
  reference to `PSX/Sparkles` anywhere; note `PSX/Sparkle`, singular, is a
  live *material* and is a different thing). Audit says the plugin is safe to
  drop — `createParticleSystem` has zero hits repo-wide, and the only
  `Ogre::ParticleSystem` mentions are two defensive teardown calls
  (`Renderer.cpp:637` type-string skip, `:1023` `destroyAllParticleSystems`)
  that are no-ops when none exists; the engine's own particles use
  `Ogre::BillboardSet`. Three places must change together, queued behind the
  P1 port because the first is in a file that port owns. Two of the three
  landed with P4: the `loadPlugin` in `RenderCore.cpp` and the
  `add_dependencies` in `CMakeLists.txt` are gone, with a comment at the
  removal site saying the plugin is still built and deliberately never loaded.
  `cmake/Dependencies.cmake:161`+`:278` (`OGRE_BUILD_PLUGIN_PFX ON` → `OFF`) is
  **left alone on purpose**: flipping a CPM/OGRE cache option forces an OGRE
  reconfigure and a multi-minute rebuild of a from-source dependency, and an
  unloaded plugin costs nothing at runtime.
- D14 — mapped, not applied:
  `docs/design/2026-07-31-asset-naming-map.md`. 91 file renames (7 engine,
  71 game, 12 demo — most of the demo ones are duplicates P4 deletes anyway),
  ~90 material renames of ~121. **Zero basename collisions** after rename;
  the only real one is the already-known `Game/PortalDown`, resolved as
  `Game/Vfx/PortalDown` vs `Demo/Vfx/PortalDown`.

  **A blocker the design doc missed:** D14 assumed `__` was the only case of a
  name prefix doubling as a mechanism. It is not. `Sprite/` does the same and
  is worse — it is both a visibility filter (`Renderer.cpp:539`) *and* four
  hardcoded base-material literals in `createSpriteMaterial`
  (`Renderer.cpp:711-714`, `log::fatal` if missing). The map calls this
  **blocked**; on review it is *coupled*, not blocked — under the derived rule
  the filter prefix and the four literals all become `Engine/Sprite/` in one
  commit, exactly like D13's flip/counter-flip pair. Code and data change
  together: a sequencing constraint, not an obstacle. The `__` filter
  itself is `Renderer.cpp:526-537` and is pure UI visibility (feeds the editor
  material picker and the DebugTools swatch list), with three consuming
  literals: `MaterialPreview.cpp:102`, `PreviewBridge.cpp:178`,
  `VfxShaderAssetTests.cpp:221`.

  Rename order (map §5): P4 dedupe first, then TOML/material-only renames,
  then docs files, then `__`→manifest `internal` as its own PR, then the
  blocked `Sprite/*`, then the `.map`-touching `Kit/*` batch **last** — it is
  the only batch that forces a re-cook (`tech_demo.map` embeds
  `meshes/kit/{Door_01,Floor_Tiles,Pillar,Spikes,Wall_01}.obj` and materials
  `Kit/{Doors,Dungeon,DungeonTwoSided,Stone}`, confirmed via `strings`).

Gate: `assetlint` (with the new naming check), visual-test clean.

**P7 — Importer registry.** Add `MeshImporter` + `registerMeshImporter`, wrap
`ObjLoader` as `ObjMeshImporter`, add `Renderer::loadMesh`, mark `loadObj`
deprecated-forwarding. No call site moves yet. Gate: ctest, visual-test.

**P8 — Move the 54 call sites** to `loadMesh`, in batches by owning system
(kit/dungeon, props, enemies, viewmodel, editor preview, demo). Delete
`loadObj`. Gate: per batch, `xvfb-run make screenshot` on the affected app.

**P9 — Logical ids.** *(Survey corrections: it is 99 `.obj` literals in data,
not 90, and 47 `loadObj` call sites, not 54 — the 54 counted declarations and
log strings too. Two more traps: the checked-in `tech_demo.map` is **already
stale** — re-cooking it unchanged shifts 110 bytes, one float per architectural
entity, `-2.384e-07` → `0.0` — so P9 must refresh it in its own commit first or
that unrelated drift hides inside the phase diff. And `assetlint.py:130-132`'s
`OBJ_RE` rule stops matching anything the moment extensions are stripped: a
live content gate that dies silently unless rewritten in the same phase.)*

Strip extensions from the `"*.obj"` literals and from
`kit.toml`/`prototypes.toml`/level TOMLs; delete `mesh_dir`; add `[formats]`
resolution. Re-cook `tech_demo.map` and any other `.map` (paths inside them
carry extensions). Keep the trailing-extension shim through this phase, delete
it at the end. Gate: `make cook VALIDATE=1`, `make scene`, editor screenshot.

**P10 — UV normalisation. — REPLANNED; the original entry was wrong twice.**

Original text: "move the V flip into `ModelImportOptions::flipV` and delete
`kit.material`'s four counter-flips, **in one commit**; gate is purely
`make visual-test`". Both halves fail on inspection (survey verified by me):

- **It is 7 passes, not 4+, and all 7 are in `kit.material`** — no other
  `.material` in the repo carries the `uvScale 1 -1 / uvOffset 0 1` pair.
- **It cannot be pixel-neutral in one commit.** `boss_arena_features.toml:44`
  and `:128` draw the *same mesh* `meshes/kit/Floor_Tiles.obj` with
  `Fantasy/Water` and `Fantasy/Lava`, which have **no `uvScale`/`uvOffset`
  uniform at all** (their shader is `PixelVfx/LiquidVS`). So one mesh needs the
  flip under `Kit/*` and not under `Fantasy/*`. A per-asset `flipV` cannot
  express "flipped for this material, not that one" — the flip is baked into
  vertices, the counter-flip is per-material.
- **The named gate cannot see the breakage.** `make visual-test` boots the
  *procedural* level and never reaches `loadPrimitiveShowcase`, so the two
  exhibits that P10 would break are off-camera for the only gate the doc cites.

Do it as two commits with a scoped, deliberately re-blessed diff on those two
exhibits, and add a capture that actually renders them before starting.
Detail and options: `docs/design/2026-07-31-mesh-importer-plan.md`.

**P11 — Install + docs.** `install()` rules; verify a staged install runs from
outside the source tree with no `PSX_ASSET_ROOT` set. Rewrite the content
sections of `ARCHITECTURE.md` and `docs/assets-pipeline.md`; add
`docs/asset-system.md`: root discovery, packs, mount order, the three data
tiers, the naming rule, adding a pack, adding an asset, **adding a format**
(the `MeshImporter` walkthrough).

---

## 6. Verification gates

Every phase:

```sh
cmake --build build -j8                      # never clean, never interrupt
ctest --test-dir build                        # 85/85 incl. layering + assetlint
python3 tools/assetlint.py
make visual-test                              # the image is frozen: zero diff
```

Per-app, on screen (memory: `game`/`psx_demo` go black on the real display —
capture under `xvfb-run`):

```sh
xvfb-run -a make screenshot APP=game       FRAME=200
xvfb-run -a make screenshot APP=scene_editor FRAME=200
xvfb-run -a make screenshot APP=psx_demo   FRAME=200
```

Plus the boot warmup line (`Warmup: N materials, 0 unsupported`) — the cheapest
proof that no material lost a texture or a shader in the move.

---

## 6.5 Tooling hazard found during P4 — do not run `clang-format` blindly

The repo's `.clang-format` pins no version and there is no format target, and
the tree was formatted with an older clang-format than the one now on PATH
(22.1.8). Measured: running 22.1.8 on `engine/src/render/Renderer.cpp`, a file
**nobody has touched**, rewrites **747 lines**. So "run clang-format on what you
touched" reformats large regions the change never went near, and the phase diff
stops being reviewable. P4's `main.cpp`/`ShowcaseScene.cpp`/`RenderCore.cpp`
diffs carry that noise; behaviour is unaffected and both binaries were verified
to produce byte-identical output.

Do not instruct further phases to run it. Fixing it properly means pinning a
version (CI + a `make format` target) — worth doing, out of scope here.

## 7. Risks

| Risk | Mitigation |
|---|---|
| Ogre's flat namespace collides after mounting packs together | Enumerated: exactly one (`Game/PortalDown`). `assetlint` gains a cross-pack check inside each mount set, so a future one fails the build |
| A moved texture silently falls back to `PINKY` instead of erroring | Already logged per binding in `RenderCore`; the warmup guard turns it into a non-zero exit |
| Cooked `.map` paths break in Part I | They are pack-root-relative (`meshes/kit/...`) and packs keep their internal shape. Verified against `tech_demo.map`. Part I needs **no re-cook**; P9 does, and owns it |
| P10 shifts a pixel and the frozen image moves | The flip and the counter-flip land in one commit, so the net transform is identity by construction. `make visual-test` is the gate, and revert is the response |
| A rename breaks a reference the lint does not model (e.g. a material named from C++) | P6 renames are done with `git mv` + a repo-wide grep including `.cpp`; `assetlint` resolves TOML/material refs, the warmup guard catches the rest at boot |
| The deprecated `loadObj` forwarder outlives its phase | It is deleted in P8, in the same PR that empties it — not left with a TODO |
| `.scn`/`.map` and `playtest.log` written by the editor land in the wrong place | `assets::project()` replaces the `../..` climb explicitly in P3 |
| Long OGRE rebuild triggered by touching a widely-included header | `eng/assets/AssetRoot.h` is a new leaf header; do not add it to `eng/Engine.h`'s public includes |
| Phase-sized diffs become unreviewable | One pack per phase, `git mv` only, each ending on a green ctest + visual-test |

---

## 8. What this buys

- One place to look. `assets/` is the answer for every app and every tool.
- 58 duplicate files and ~3.4 MB gone; the demo stops drifting from the game.
- Relocatable builds — an installed game becomes possible for the first time.
- Ogre stops registering authoring debris as resource locations.
- A declared manifest, a mount order, and a single `resolve()` — the three
  seams smart loading (streaming, paks, refcounting, hot reload) needs, without
  building any of it yet.
- **GLTF becomes an importer registration.** Today it is 54 call sites, 90 data
  literals and a re-cook. After P9 the whole cost of a new mesh format is one
  `MeshImporter` subclass and one line in `[formats]` — which is also exactly
  what the viewmodel work in `CLAUDE.md` Phase 7 assumes it can rely on.
- One image format, one particle stack, one naming rule, three named data
  tiers — each with a lint that fails the build rather than a convention
  someone remembers.

## 9. Open question for the owner

D10 keeps `.scn` as JSON, so authored data is TOML but editor documents are
not. The alternative — all-TOML, deleting `scene.schema.json` and reworking
`SceneDocument`/`SceneWriter`/`SceneValidate` + two test files — buys
uniformity and costs a working validation gate. The plan takes the cheaper
option; say so if you would rather pay for the uniform one.
