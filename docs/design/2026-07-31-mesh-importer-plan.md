# P7–P10 Execution Plan — Mesh Importer Registry, Logical Ids, UV Normalisation

Status: survey, 2026-07-31, branch `major-refactor`.
Companion to `docs/design/2026-07-31-unified-asset-root.md` (D8, D9, D13, §4.3,
phases P7–P10). Read-only survey; nothing here was built or changed.

Purpose: answer, up front, the questions P7–P10 would otherwise discover
mid-refactor, and correct the parts of §4.3 that were written from
`ObjLoader.h` rather than `ObjLoader.cpp`.

**Headline verdicts**

1. There are **47** `loadObj` call sites, not 54. Thirteen files is right.
2. `MeshData` as sketched in §4.3 is wrong in four ways; the biggest is that it
   collapses two vertex streams the loader deliberately keeps apart.
3. **P10 is not a single pixel-neutral commit.** Two exhibits draw a *kit* mesh
   with a *non-kit* material, so no per-asset `flipV` can reproduce today's
   image for both. A concrete verdict and three options are in §4.
4. The checked-in `tech_demo.map` **already does not reproduce** from its
   `.scn`. Re-cooking it today changes 110 bytes before P9 touches anything.

---

## 1. Ground truth: what P7–P10 actually operates on

| Thing | Reality |
|---|---|
| `loadObj` call sites | **47**, in 13 files (§2) |
| `loadObj` overloads | 3 declared; the `ModelImportOptions` one has **one** caller, which is itself dead (§3.5) |
| meshes that feed physics | **zero** in shipping code (§3.5) |
| `.obj` literals in data | **99** across 5 TOMLs (§5.1) — the doc says 90 |
| counter-flipped material passes | **7**, all in `kit.material` (§4.1) — the doc says "4+" |
| cooked `.map` files | exactly one, `game/assets/scenes/tech_demo.map` |
| `.scn` files carrying an extension | none — they reference prefab ids already (§5.4) |

---

## 2. The 47 call sites, and the P8 batch plan

Every site below calls `Renderer::loadObj`. Column *ov* is the overload:
`p` = `loadObj(path)`, `b` = `loadObj(path, const glm::mat4* bake)`,
`o` = `loadObj(path, const ModelImportOptions&)`.
Column *phys* is whether the returned handle ever reaches
`Renderer::meshCollisionGeometry`.

### 2.1 Batch A — dungeon kit + procedural level (`game`)

| Site | ov | Path construction | phys |
|---|---|---|---|
| `game/src/DungeonMap.cpp:214` | b | `kitMeshDir + "Floor_Tiles.obj"` | no |
| `game/src/DungeonMap.cpp:225` | b | `kitMeshDir + "Wall_01.obj"` | no |
| `game/src/DungeonMap.cpp:227` | b | `kitMeshDir + "Wall_02.obj"` | no |
| `game/src/DungeonMap.cpp:233` | b | `kitMeshDir + "Door_Frame_01.obj"` | no |
| `game/src/DungeonMap.cpp:245` | b | `kitMeshDir + "Pillar.obj"` | no |
| `game/src/DungeonMap.cpp:246` | p | `propMeshDir + "prop_torch.obj"` | no |
| `game/src/DungeonMap.cpp:318` | p | `propMeshDir + mesh` (mesh from `dungeon_props.toml`) | no |

`kitMeshDir`/`propMeshDir` arrive from `LiveLevel.cpp:76-77` as
`game::assetDir("meshes/kit")` — an *absolute* dir with a trailing `/`
(`game/src/GameAssets.h:39-42`). So the concatenation is
absolute-dir + filename; `loadMesh` wants `"kit/wall_01"`, which means the two
`DungeonMap::load` parameters (`DungeonMap.h:47`) disappear rather than change
type. Batch A is the only batch where the signature churn is non-trivial.

**Verified by:** `xvfb-run -a make screenshot SHOT=... FRAME=200` (default
`./build/game`, procedural dungeon).

### 2.2 Batch B — scene dressing, portals, shrine (`game`)

| Site | ov | Path construction | phys |
|---|---|---|---|
| `game/src/SceneFactory.cpp:88` | b | `style.kitMeshDir + "Pillar.obj"` | no |
| `game/src/SceneFactory.cpp:102` | b | `style.kitMeshDir + "Arch.obj"` | no |
| `game/src/SceneFactory.cpp:264` | p | `path.parent_path() / meshPath` — **relative to the TOML file**, not to a pack | no |
| `game/src/SceneFactory.cpp:432,433,434` | p | `props + "prop_vase_p0/p1/jutesack.obj"` | no |
| `game/src/SceneFactory.cpp:452` | p | `props + "prop_chest.obj"` | no |
| `game/src/SceneFactory.cpp:463` | p | `meshDir + "crystal_ground.obj"` | no |
| `game/src/SceneFactory.cpp:465-468` | p | `meshDir + "crystal_spire{1..4}.obj"` | no |
| `game/src/SceneFactory.cpp:489,490` | p | `props + "prop_barrel_open_p{0,1}.obj"` | no |
| `game/src/Dummy.cpp:51` | p | `game::assetPath("meshes/props/prop_haybale.obj")` — already resolved | no |
| `game/src/PropSystem.cpp:43,44,45` | p | `assetDir("meshes/props") + "prop_*.obj"` | no |

`SceneFactory.cpp:264` is the odd one and P9 must handle it explicitly: it is
the `shape = "asset_mesh"` branch of `loadPrimitiveShowcase`, and it joins the
mesh string to the *directory of the TOML being parsed*. That works today only
because `boss_arena_features.toml` sits at the game pack root and writes
`mesh = "meshes/kit/Candle_02.obj"`. Under D9 it becomes
`assets::resolve()` on a logical id and the `parent_path()` join is deleted.

**Verified by:** `game` frame 200 (dressing, crystal ring, braziers, portals)
**and** `make scene SCENE=game/assets/scenes/ritual_boss_showroom.scn` for the
`asset_mesh` exhibits, which the default `game` screenshot does **not** show
(§7.2).

### 2.3 Batch C — viewmodel + enemies (`game`)

| Site | ov | Path construction | phys |
|---|---|---|---|
| `game/src/ViewModel.cpp:118` | b | `meshPath` param; caller at `:82` passes `propsDir + "/prop_sword.obj"` | no |
| `game/src/ViewModel.cpp:177` | p | `crystalMeshPath` param | no |
| `game/src/enemy/EnemySystem.cpp:212` | p | `assetPath(def->visual.mesh)` from `enemies.toml` | no |

This is the batch `CLAUDE.md` Phase 7's GLTF viewmodel work sits on top of.
Keep it separate and land it last of the `game` batches so the viewmodel work
branches from a finished `loadMesh`.

**Verified by:** `game` frame 200 (the viewmodel is always on screen); enemies
need a frame late enough for a spawn, or the debug console.

### 2.4 Batch D — authored `.map` playback (`game`)

| Site | ov | Path construction | phys |
|---|---|---|---|
| `game/src/LiveLevel.cpp:59` | p | raw `path` from `MeshSource`, if it exists on disk | no |
| `game/src/LiveLevel.cpp:62` | p | `assets::resolve(path)` fallback | no |
| `game/src/MapPlay.cpp:80` | p | same pattern | no |
| `game/src/MapPlay.cpp:83` | p | same pattern | no |

These four are a single `resolveMeshes` lambda duplicated in two files. They
are the **only** sites whose input is data-driven from a cooked `.map`, so they
are the ones that must keep working across P9's extension strip. Do them
*with* P9, not in P8: the "exists on disk, else resolve" two-step is exactly the
trailing-extension shim D9 describes, and it should be replaced by a single
`loadMesh(id)` in the same commit that re-cooks the map.

**Verified by:** `make scene SCENE=game/assets/scenes/tech_demo.scn`.

### 2.5 Batch E — editor preview

| Site | ov | Path construction | phys |
|---|---|---|---|
| `game/editor/PreviewBridge.cpp:61` | p | `assets::resolve(path)` where `path` is a `MeshSource` | no |

One site, already resolver-based. Trivial.

**Verified by:** `xvfb-run -a make screenshot APP=scene_editor FRAME=200`
(via `tools/visual_test.py --app scene_editor`).

### 2.6 Batch F — samples

| Site | ov | Path construction | phys |
|---|---|---|---|
| `samples/psx-demo/src/ShowcaseScene.cpp:214` | p | `meshPath("meshes/" + "crystal_ground.obj")` | no |
| `samples/psx-demo/src/ShowcaseScene.cpp:246` | p | `meshPath(meshes + shard.mesh)` | no |
| `samples/psx-demo/src/ShowcaseScene.cpp:331` | b | `meshPath(kit + "Pillar.obj")` | no |
| `samples/psx-demo/src/ShowcaseScene.cpp:344` | b | `meshPath(kit + "Arch.obj")` | no |
| `samples/psx-demo/src/ShowcaseScene.cpp:438` | p | `meshPath(props + "prop_chest.obj")` | no |
| `samples/psx-demo/src/ShowcaseScene.cpp:536` | p | `meshPath(props + file)` | no |
| `samples/psx-demo/src/ShowcaseScene.cpp:652,654` | p | `meshPath(props + "prop_beam/lamp.obj")` | no |
| `samples/common/DemoScene.cpp:141` | p | `meshDir + strAt(shaft,"mesh")` | no |
| `samples/common/DemoScene.cpp:160` | p | `meshDir + strAt(boxes,d.meshKey)` | no |
| `samples/common/DemoScene.cpp:176` | p | `meshDir + strAt(crystals,"ground_mesh")` | no |
| `samples/common/DemoScene.cpp:196` | b | `meshDir + strAt(spire,"mesh")` | no |

`ShowcaseScene::meshPath` (`:22-25`) is already `assets::resolve` — it becomes
the identity and is deleted. `DemoScene` is **not** a sample: it is loaded by
`LiveLevel.cpp:131` into every procedural game level, with
`meshDir = game::assetDir("meshes")`. Its four sites therefore change the
`game` image as well as `psx_demo`'s, and the `meshDir` parameter of
`DemoScene::load` (`samples/common/DemoScene.h:29`) goes away.

**Verified by:** `psx_demo` frame 200 for `ShowcaseScene`; **both** `psx_demo`
and `game` frame 200 for `DemoScene`.

### 2.7 Batch G — engine + dead code

| Site | ov | Note |
|---|---|---|
| `engine/src/render/Model.cpp:40` | **o** | the only `ModelImportOptions` caller — and `spawnModel` has zero callers repo-wide (§3.5) |
| `game/src/LobbyDressing.cpp:83` | p | inside `loadLobbyDressing`, which has **zero callers**; `showroom_props.toml` is orphaned with it |

Do this batch first: it is two sites, neither of which can move a pixel.
Decide there whether `loadLobbyDressing` and `showroom_props.toml` are deleted
rather than ported. `Renderer::loadObj(path, const ModelImportOptions&)`
survives only for `spawnModel`; `loadMesh(id, const ModelImportOptions&)`
should still exist (P9 needs `flipV` on it) but it will have one dead caller
until something uses `spawnModel`.

### 2.8 Ordering

```
G (dead/engine)  →  E (editor)  →  F (samples)  →  B, C (game dressing, viewmodel)
                 →  A (kit/procedural)  →  D (authored map, folded into P9)
```

G/E/F/B/C are independent. A is last of the render-only batches because it is
the one that changes public signatures (`DungeonMap::load`). D is deferred into
P9 by design.

---

## 3. What `MeshData` must actually carry — §4.3 corrected

Read `engine/src/render/ObjLoader.cpp` end to end before implementing. Four
things the §4.3 sketch gets wrong.

### 3.1 It collapses two vertex streams that must stay separate

`ObjLoader::upload` (`:220-274`) emits **one vertex per face corner**:

```cpp
manual->position(vertex.position);
manual->normal(...); manual->textureCoord(...); manual->colour(vertex.colour);
renderFace.push_back(nextIndex++);        // ObjLoader.cpp:265
```

`captureGeometry` (`:199-218`) emits the **source position array** with the
source indices:

```cpp
for (const Vert& vertex : parsed.positions)
    outVerts.push_back({...});            // ObjLoader.cpp:206-208
...
detail::appendTriangleFan(positions, outIndices);   // :216
```

These are different vertex counts for the same mesh — necessarily, because OBJ
indexes position, uv and normal independently. `engine/tests/ObjGeometryTests.cpp:45`
pins it: a two-triangle quad must yield **4** vertices, not 6.

A single `positions/normals/uvs/colours/indices` struct cannot represent both.
`MeshData` must carry the render stream *and* the collision stream, mirroring
`eng::detail::MeshGeometry` (`engine/src/render/MeshResources.h:15-18`), which
is what `Renderer::meshCollisionGeometry` already hands out.

### 3.2 The colour stream is a hard invariant, and it lives in the upload

`engine/assets/shaders/psx.vert:12` declares `in vec4 colour` unconditionally
and `psx.frag:83` multiplies by it:

```glsl
vec4 color_base = vColour * vec4(toLinear(modulateColor.rgb), modulateColor.a);
```

An unbound colour attribute is black geometry. Today the guarantee is
structural, not conditional: `Vert::colour` defaults to `{1,1,1,1}`
(`ObjLoader.cpp:27`) and `upload` calls `manual->colour(...)` for every corner
whether or not the file had colours. Only **7** meshes in the tree use the
`v x y z r g b a` extension (`crystal_ground`, `crystal_spire1..4`,
`light_shaft`, `props/prop_torch`); the other ~90 rely on the white default.

§4.3's comment `// white when the source has none` is right about the value and
silent about where it is enforced. Put the enforcement in the **registry**, not
in each importer: after `import()` returns, resize `colours` to
`positions.size()` filled with `vec4(1)` if the importer left it empty. A future
`GltfImporter` then cannot forget.

`ProceduralMeshes::upload` (`engine/src/render/ProceduralMeshes.cpp:7-26`)
already takes exactly the indexed shape `MeshData` wants and already always
writes a colour. Shape `MeshData` like `eng::detail::PrimitiveGeometry`
(`PrimitiveGeometry.h:13-23`) and one upload function serves both paths.

### 3.3 The bake is not "apply the matrix"

Two distinct entry points exist and the registry must keep both:

- `load(path, name, const Ogre::Matrix4& bake, ...)` — the caller supplies the
  matrix outright (11 of the 47 call sites).
- `load(path, name, const ModelImportOptions&, ...)` — the matrix is *derived*,
  after parsing, from the source positions:
  `parseCanonical` (`:184-197`) calls
  `eng::modelImportBakeMatrix(options, sourcePositions(parsed))`.
  `BoundsCenter`/`BottomCenter` pivots need the parsed geometry, so this cannot
  be hoisted above the importer.

Normals get the inverse-transpose, computed in Ogre and **not** re-normalised
against the source: `transform` (`:172-182`) does
`bake.linear().Inverse().Transpose()` then `normal.normalise()`.
`eng::modelImportNormalMatrix` (`ModelImport.h:239-242`) is the glm equivalent
and is currently unused by the loader — if the registry switches to it, the
result is bit-comparable but not bit-identical, so change it in a commit of its
own or not at all.

So `MeshImporter::import` must receive the *options*, not a matrix, and be
allowed to compute the bake itself. §4.3 says "`options` is already sanitized"
— true and worth keeping (`sanitizeModelImportOptions` is applied at
`Renderer.cpp:215`), but it must also say the importer owns the bake derivation.

### 3.4 Polygon handling, and what the sketch omits

- **Fan triangulation, twice, in two winding conventions that must agree.**
  Render: `for (size_t i = 2; i < renderFace.size(); ++i) manual->triangle(renderFace[0], renderFace[i-1], renderFace[i]);`
  (`ObjLoader.cpp:267-269`). Collision: `detail::appendTriangleFan` (`:280-288`),
  same order. `ObjGeometryTests.cpp:51-53` asserts the collision fan matches the
  render winding. Keep `appendTriangleFan` exported for that test.
- **Out-of-range face indices are skipped silently** — render at `:249-251`,
  collision at `:213-215`. The two skip independently, so a malformed file can
  already produce a render mesh and a collision mesh that disagree. Preserve
  the behaviour; do not "fix" it inside P7.
- **Negative OBJ indices are supported** (`parseFaceRef`, `:90`:
  `index = index > 0 ? index - 1 : count + index`).
- **Missing uv/normal fall back to `Vector2::ZERO` / `UNIT_Y`** (`:255-263`).
- **`.mtl` is parsed and thrown away.** `mtllib` fills
  `ParsedObj::materialLibraries` (`:142-145`) and `usemtl` fills `Face::material`
  (`:146-147, :150`); nothing reads either. `upload` begins one submesh with
  `"BaseWhite"` and says so at `:226-228`. In the whole tree there is **one**
  `usemtl` (`game/assets/meshes/box.obj`, `usemtl None`) and **zero** `.mtl`
  files. So `defaultMaterial` in §4.3's sketch is speculative; keep it as a
  single optional string if you like, but do not build per-face material
  remapping for a corpus that has none.
- **No tangents, no submeshes, no skinning.** One submesh, always. GLTF will
  want submeshes eventually — leave the door open (`std::vector<SubMesh>` with
  one entry) but do not implement multi-submesh attachment now:
  `Renderer::attachMesh` and `resolveModelMaterialForSubmesh` assume submesh 0
  (`Model.cpp:80-86`).

### 3.5 `loadGeometry` is not a second parse of a live path — and physics is unhooked

`ObjLoader::loadGeometry` (`:311-342`) *is* a second, independent parse of the
same file, but nothing in the shipping build calls it. The only callers are
`engine/tests/ObjGeometryTests.cpp:43, 70, 106`. The runtime collision stream is
captured during the render parse (`Renderer.cpp:193-194, 226-227` pass
`&geometry.vertices, &geometry.indices` into `load`), which is exactly what
`Renderer.h:95-96` claims: *"captured during the render-mesh load, never
reparsed"*.

Bigger finding: **no shipping code consumes mesh collision geometry at all.**
`Renderer::meshCollisionGeometry` has one caller, `Model.cpp:66`, inside
`eng::spawnModel` — and `spawnModel` is declared at `Model.h:247`, defined at
`Model.cpp:10`, and **called nowhere in the repository**, tests included.
Cooked-map collision is authored box volumes derived from the kit socket
(`game/content/SceneCook.cpp:16-30`), not mesh triangles.

Consequences for P7–P10:

- Removing `loadGeometry` is **safe**, but it is a test-API change:
  `ObjGeometryTests` must be rewritten against `MeshImporter::import` +
  `MeshData::collision`. Do that inside P7, where the test is the only thing
  that proves the registry preserves the two streams.
- The collision capture costs a second traversal on every mesh load for a
  consumer that does not exist. Deleting it would be a behaviour change to a
  public API (`meshCollisionGeometry` would start returning false); **do not**
  fold that into P7–P10.
- P8's screenshot gate is sufficient for physics *today*. It will stop being
  sufficient the moment anything calls `spawnModel` with
  `ColliderMode::StaticMesh`. See §7.1.

### 3.6 Proposed interface

```cpp
namespace eng {

// One vertex per face corner: the render stream. Parallel arrays, all the same
// length, indexed by `indices`. `colours` is guaranteed non-empty and
// vec4(1) by the registry, because psx.frag multiplies by it and an unbound
// colour attribute renders black.
struct MeshVertexStream {
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec4> colours;
    std::vector<uint32_t>  indices;
};

struct MeshData {
    MeshVertexStream render;
    // The source's own shared positions, fan-triangulated in the render's
    // winding order. NOT derivable from `render` -- see ObjGeometryTests, which
    // pins a quad at 4 collision vertices against 6 render vertices.
    // May be empty: an importer that has no cheaper representation may leave it
    // so, and meshCollisionGeometry then reports false as it already does.
    detail::MeshGeometry collision;
    // The bake the importer derived from `options` + the parsed positions.
    // Returned so callers can reason about the pivot without re-deriving it.
    glm::mat4 bake{1.0f};
    std::string defaultMaterial;   // effectively always empty: no .mtl in tree
};

class MeshImporter
{
public:
    virtual ~MeshImporter() = default;
    // Lower-case, with the dot: ".obj". Matched against the resolved file's
    // extension, not against the logical id.
    virtual std::span<const std::string_view> extensions() const = 0;
    // `options` is sanitized. The importer derives its own bake via
    // eng::modelImportBakeMatrix(options, sourcePositions) -- BoundsCenter and
    // BottomCenter need the parsed geometry, so the caller cannot hoist it.
    // It also applies its format's own conventions, including the V flip
    // (D13), which is why `options` carries flipV.
    virtual bool import(const std::filesystem::path&,
                        const ModelImportOptions&, MeshData&) = 0;
};

void registerMeshImporter(std::unique_ptr<MeshImporter>);

} // namespace eng
```

`Renderer`:

```cpp
MeshHandle loadMesh(const std::string& id);
MeshHandle loadMesh(const std::string& id, const glm::mat4* bake);
MeshHandle loadMesh(const std::string& id, const ModelImportOptions&);
```

Header placement: `engine/include/eng/render/MeshImporter.h`. `tools/check_layering.py`
files `eng/render/` under the catch-all `("", "systems")` rule
(`check_layering.py:90`), the same layer as `Renderer.h`, and
`engine/src/render/*.cpp` is `systems` via `("src/", "systems")` (`:47`). **No
`HEADER_RULES` change is needed** — unlike `eng/assets/`, which needed one in P0.
Do *not* put `MeshData` under `eng/assets/`: that directory is pinned to `core`
(`check_layering.py:81`) and could not include `eng/render/ModelImport.h`.

### 3.7 What §4.3 says vs. what is true

| §4.3 claim | Correction |
|---|---|
| "54 call sites across 13 files" | **47** call sites; 13 files is right |
| `MeshData` = one flat vertex/index set | two streams; the collision one is source-indexed and pinned by a test (§3.1) |
| "white when the source has none" | true, but the guarantee lives in the upload, not the parse, and should move to the registry (§3.2) |
| `defaultMaterial` "may be empty" | it is *always* empty: zero `.mtl` files, one `usemtl` in the tree (§3.4) |
| "the importer applies the bake" | the importer must **derive** the bake for the options overload; a pre-computed matrix is only possible for the `glm::mat4*` overload (§3.3) |
| "The mesh cache keys off `modelImportCacheKey()` … no change needed there" | **There is no mesh cache.** `Renderer.cpp:238-240` says so explicitly: *"every load still owns a distinct Ogre resource/handle"*. `modelImportCacheKey` produces an identity string stored in `MeshResources::Record::importIdentity`, which is written and never read. P9 additionally breaks it: it calls `weakly_canonical` on its path argument (`ModelImport.h:257, 91-99`), and a logical id is not a path |
| §7 risk "the deprecated `loadObj` forwarder outlives its phase" | fine, but note the `ModelImportOptions` overload has exactly one caller, itself dead (§3.5) |

---

## 4. D13 / P10 — the UV flip

### 4.1 Every counter-flipping pass in the repo

`ObjLoader.cpp:136` flips unconditionally at parse time:

```cpp
values >> uv.x >> uv.y;
uv.y = 1.0f - uv.y;
```

`psx.vert:70` then applies the material's transform:

```glsl
vUV = uv0 * uvScale + uvOffset;
```

The counter-flip pair `uvScale float2 1.0 -1.0` + `uvOffset float2 0.0 1.0`
appears **7 times, all in `game/assets/materials/kit.material`**, and nowhere
else in any `.material` file in the repo:

| Material | Lines |
|---|---|
| `Kit/Dungeon` | 33-34 |
| `Kit/DungeonTwoSided` | 54-55 |
| `Kit/Doors` | 75-76 |
| `Kit/Containers` | 95-96 |
| `Kit/Metal` | 115-116 |
| `Kit/Wood` | 135-136 |
| `Kit/Stone` | 158-159 |

The file's own header comment (`kit.material:9-23`) states the reason and even
the exit condition: *"If the kit is ever re-exported bottom-up, delete these two
params."*

### 4.2 Every other `uvScale`/`uvOffset` — real tiling and atlas offsets, must not be touched

| Material | Site | Value | Meaning |
|---|---|---|---|
| `Game/Room` | `game.material:7` | `uvScale 6.0 6.0` | tiling |
| `Game/DungeonFloor` | `game.material:78-79` | `uvScale 11.0 11.0`, `uvOffset -9.0 -3.0` | tiling **+ atlas offset** |
| `Game/DungeonCeiling` | `game.material:96-97` | `uvScale 11.0 11.0`, `uvOffset -10.0 -3.0` | tiling **+ atlas offset** |
| `Game/DungeonWall` | `game.material:119-120` | `uvScale 3.0 3.0`, `uvOffset 0.0 0.0` | tiling |
| `Fantasy/CarvedStone` | `fantasy_surfaces.material:9` | `uvScale 2.0 2.0` | tiling |
| `Fantasy/WarmDungeonStone` | `fantasy_surfaces.material:23` | `uvScale 2.5 2.5` | tiling |
| `Fantasy/AgedWood` | `fantasy_surfaces.material:51` | `uvScale 1.5 1.5` | tiling |
| `Game/PrototypeFloor` | `prototype.material:8` | `uvScale 16.0 16.0` | tiling |
| `PSX/Floor` | `demo.material:11` | `uvScale 24.0 24.0` | tiling |
| `PSX/ShowcaseStone` | `showcase.material:67` | `uvScale 3.0 3.0` | tiling |
| `PSX/PortalBacking` | `showcase.material:89` | `uvScale 2.0 2.0` | tiling |

None of these has a negative component. Every one of them keeps
`uvOffset` at the program default `0.0 0.0` unless listed above. They are
orthogonal to D13 and must survive P10 untouched.

### 4.3 The algebra

For a kit mesh drawn with a `Kit/*` material, the current net transform is:

```
v_file  --loader-->  1 - v_file  --shader-->  (1 - v_file)*(-1) + 1  =  v_file
```

Exactly the identity. So `flipV = false` on the kit meshes, plus deleting the 7
counter-flips, reproduces today's image for those meshes. This is not
approximate: `uvScale`/`uvOffset` default to `1,1`/`0,0` in all three
`PSX_VS_*` programs (`psx.program:16-17, 31-32, 56-57`), so with the params gone
the shader is `vUV = uv0`.

One caveat worth stating rather than discovering: `1.0f - (1.0f - v)` is not
bit-exact for every float `v` (it is exact only where Sterbenz applies, i.e.
`v ∈ [0.5, 1]`). For `Wall_01`'s V minimum of `0.0063` the round-trip error is
≈ 1 ulp ≈ 6e-8, which is ~1.5e-5 of a texel on a 256×256 atlas with
`filtering none`. It cannot flip a nearest-neighbour sample except exactly on a
texel boundary, and none of the authored sub-rectangles lands there. Expect zero
pixel diff; if `visual-test` reports a handful of edge pixels, this is the cause
and it is benign — but per the doc's own rule, revert rather than debug forward,
then re-land with the diff explained.

For `Kit/Stone`'s two out-of-range pieces (`Arch_Roof` V ∈ [-3.25, 4.25],
`Hexagon` V ∈ [0.0013, 0.99]) the identity holds just the same; range does not
enter the algebra.

### 4.4 The blocker: one kit mesh, three materials

`game/assets/boss_arena_features.toml` draws `meshes/kit/Floor_Tiles.obj` with
non-kit materials:

```toml
# :44-46
shape = "asset_mesh"
mesh = "meshes/kit/Floor_Tiles.obj"
material = "Fantasy/Water"

# :128-130
mesh = "meshes/kit/Floor_Tiles.obj"
material = "Fantasy/Lava"
```

`Fantasy/Water` and `Fantasy/Lava` (`vfx.material:94-133`) do **not** use
`PSX_VS_Lit`. They use `PixelVfx/LiquidVS` (`vfx.program:65-71`), whose shader
`engine/assets/shaders/liquid.vert` is four lines long and has **no** `uvScale`
or `uvOffset` uniform at all:

```glsl
liquidUV = uv0;
```

`liquid.frag:16-26` then samples a wrap-addressed noise texture at `pixelUV` and
`pixelUV * 1.7`. `Floor_Tiles.obj` has V ∈ [0.25, 0.50], so today those two
exhibits sample the [0.50, 0.75] band of `water_stylized.png` /
`lava_stylized.png`. With `flipV = false` on `Floor_Tiles.obj` they would sample
[0.25, 0.50] — a different, mirrored region of a tiling noise texture. Visibly
different, not sub-ulp.

The same mesh must therefore be flipped for one material and not for another.
**No per-asset `flipV` can be pixel-neutral.**

Two smaller instances of the same class:

- `Kit/*` materials are also previewed on the editor's procedural sphere
  (`MaterialPreview.cpp:281-287` uses `PrimitiveKind::Sphere`, whose UVs never
  went through the loader). Deleting the counter-flip flips the preview's V.
  Editor-only, no game pixels, but `make material` will look different.
- `samples/psx-demo/src/ShowcaseScene.cpp:331, 344` draws kit `Pillar`/`Arch`
  with `Kit/Dungeon` — consistent, so `psx_demo` is fine.

### 4.5 Verdict on P10

**P10 is not a single pixel-neutral commit as written.** Choose one:

**(a) Two commits, and accept a scoped, documented diff.** Commit 1: add
`ModelImportOptions::flipV` (default `true` for `.obj`), thread it through
`kit.toml` as a per-piece or per-`[kit]` field, set it `false` for the kit,
delete the 7 counter-flips. Commit 2, separately reviewed: re-author the two
liquid exhibits in `boss_arena_features.toml` to use a mesh whose V band gives
the intended look, or accept the new band and re-bless the ritual-showroom
screenshot. This is the honest option and the one I would take. It keeps D13's
principle intact — the importer normalises, the material does not compensate —
and it isolates the one place where "the image is frozen" genuinely cannot hold.

**(b) Keep a V transform in `LiquidVS`.** Add `uvScale`/`uvOffset` to
`liquid.vert`/`vfx.program` and set `1,-1 / 0,1` on `Fantasy/Water` and
`Fantasy/Lava`. Pixel-neutral by construction, and a direct violation of D13:
the counter-flip has been moved, not deleted, and now sits in a shader that had
no reason to know about OBJ.

**(c) Re-export the 44 kit OBJs bottom-up** and keep `flipV = true` globally.
This is what `kit.material:22-23` suggests, and it does **not** solve the
problem: rewriting `vt v → 1-v` in the source makes the kit correct *and*
changes the liquid exhibits by exactly the same amount as (a). It also touches
44 binary-ish asset files for no advantage over a boolean.

Whichever is chosen, the gate must be **wider than `make visual-test`**:

```sh
make visual-test                                     # procedural game, frame 90
xvfb-run -a make screenshot SHOT=/tmp/a.png FRAME=200 # procedural game
make cook SCENE=game/assets/scenes/ritual_boss_showroom.scn OUT=/tmp/r.map
python3 tools/visual_test.py --build-dir build screenshot --map /tmp/r.map --frame 200
xvfb-run -a python3 tools/visual_test.py --build-dir build screenshot --app psx_demo --frame 200
```

The default `make visual-test` boots `./build/game` with no arguments, which
takes the *procedural* branch of `LiveLevel::buildLevel` (`LiveLevel.cpp:71`
onward). `loadPrimitiveShowcase` — and therefore the two liquid exhibits — is
only reached on the authored-map branch at `LiveLevel.cpp:109`. **The gate the
design doc names for P10 cannot see the one thing P10 breaks.** `visual_test.py`
already accepts `--map` (`Makefile:249`); use it.

---

## 5. D9 / P9 — logical ids

### 5.1 `.obj` literals in data

99 occurrences across five files (the doc says 90):

| File | Count | Shape |
|---|---:|---|
| `game/assets/kit.toml` | 44 | `mesh = "Floor_Tiles.obj"` + `mesh_dir = "meshes/kit"` |
| `game/assets/showroom_props.toml` | 23 | `meshes = ["prop_table_p0.obj", ...]` — **orphaned data** (§2.7) |
| `game/assets/boss_arena_features.toml` | 12 | `mesh = "meshes/kit/Candle_02.obj"` — pack-relative, joined to the TOML's own dir |
| `game/assets/dungeon_props.toml` | 12 | `meshes = ["prop_chest.obj"]` |
| `samples/common/assets/demo_scene.toml` | 8 | `mesh = "light_shaft.obj"`, `metal_mesh = "bevel-box.obj"` |

Plus ~35 `.obj` string literals in C++ (see §2's path columns) and 8 in tests.

`tools/assetlint.py:61, 130-132` validates every quoted `*.obj` against the set
of `.obj` basenames on disk (`OBJ_RE`). After P9 strips the extensions that rule
matches nothing and silently stops checking. It must be rewritten in the same
phase to resolve logical ids through `[formats]` — otherwise P9 quietly removes
a working content gate.

### 5.2 `kit.toml` under D9

```toml
# before
[kit]
scale = 0.2
cell_size = 20.0
mesh_dir = "meshes/kit"

[[piece]]
id = "floor"
mesh = "Floor_Tiles.obj"
material = "Kit/Dungeon"

[[piece]]
id = "floor_hexagon"
mesh = "Hexagon.obj"
material = "Kit/Stone"
```

```toml
# after: mesh_dir is gone; the id IS the path under the pack's meshes/
[kit]
scale = 0.2
cell_size = 20.0

[[piece]]
id = "floor"
mesh = "kit/floor_tiles"
material = "Kit/Dungeon"

[[piece]]
id = "floor_hexagon"
mesh = "kit/hexagon"
material = "Kit/Stone"
```

Note the lower-casing: D14's naming rule and D9's id rule land on the same
44 strings, so the two edits should be one commit per file, not two passes.
`docs/design/2026-07-31-asset-naming-map.md` already schedules the `Kit/*`
batch last for exactly this reason.

Code that follows:

- `KitCatalog::load` (`game/content/KitCatalog.cpp:72-75`) currently *requires*
  `mesh_dir` and errors without it: `"kit scale, cell_size and mesh_dir are
  required"`. Drop it from the required set and delete `KitCatalog::meshDir()`
  (`KitCatalog.h:64`).
- `KitPiece::meshPath` (`KitCatalog.h:32`) is documented as
  `"meshes/kit/Wall_01.obj"`. Rename to `mesh` and redocument as a logical id;
  `game/tests/KitCatalogTests.cpp:43` pins the old string.
- `SceneCook.cpp:105-106` writes `MeshSource{piece->meshPath}` — this is where
  the extension enters the cooked `.map`.
- `game/src/scene/LayoutToScene.cpp:127, 145, 173, 195, 257, 267-269` hardcodes
  the same filenames for the procedural path and must move in step, or a
  procedural level and a cooked level will name the same piece differently.

### 5.3 Re-cooking `tech_demo.map` — precisely what is required

`strings` on `game/assets/scenes/tech_demo.map` confirms the design doc's list
exactly. Five mesh paths:

```
meshes/kit/Door_01.obj
meshes/kit/Floor_Tiles.obj
meshes/kit/Pillar.obj
meshes/kit/Spikes.obj
meshes/kit/Wall_01.obj
```

and four material names: `Kit/Doors`, `Kit/Dungeon`, `Kit/DungeonTwoSided`,
`Kit/Stone`. It is the **only** `.map` in the repo. `ritual_boss_showroom.scn`
has no cooked counterpart and is cooked on demand.

The command is:

```sh
make cook SCENE=game/assets/scenes/tech_demo.scn
# = ./build/scene_cook <abs .scn> --kit <abs game/assets/kit.toml> --out <abs .map>
```

**A finding that changes how P9 should verify this.** I re-cooked the shipped
scene against the current tree without changing anything:

```
./build/scene_cook game/assets/scenes/tech_demo.scn --kit game/assets/kit.toml --out /tmp/recook.map
```

Same size (62417 bytes), same `strings`, and **110 differing bytes**. The
difference is one `float` per architectural entity, on an 81-byte stride:
shipped `0x b4800000` = `-2.384185791015625e-07`, recook `0x00000000` = `0.0`.
That is `-2^-22` metres — a rounding artefact in a position or offset that some
committed change since the last cook now computes exactly.

So the checked-in `tech_demo.map` is **already stale**. Consequences:

1. P9's re-cook will absorb this drift silently. Do not let it: cook the map
   **before** editing `kit.toml`, commit that as its own "refresh stale cooked
   map" change with the 110-byte delta explained, and only then do the D9 edit.
2. With that baseline in place, the D9 re-cook has an exact expectation: the
   only bytes that may change are the five mesh-path strings (and the file
   shrinks by 4 bytes per distinct path if lengths are prefixed).
3. Verification, in order:
   - `make cook SCENE=game/assets/scenes/tech_demo.scn VALIDATE=1` — validates
     without writing.
   - re-cook, then `strings tech_demo.map | grep meshes/` — must show
     `meshes/kit/floor_tiles` etc. with no extension.
   - `cmp -l old.map new.map | wc -l` against the expected string-only delta.
   - `make scene SCENE=game/assets/scenes/tech_demo.scn` and screenshot.
   - `ctest -R CookParity` — `game/tests/CookParityTests.cpp:60-65` already
     proves the CLI and in-process cookers agree byte for byte, so a passing
     parity test means the new map is reproducible.

`game/tests/MapSerializerTests.cpp:42, 76` and
`game/tests/LayoutToSceneTests.cpp:78, 104` pin `.obj` strings and must be
updated in the same commit.

### 5.4 `.scn` carries no extensions

Both shipped scenes reference prefab ids only:

- `tech_demo.scn`: 422 entities, prefabs `kit.{door,floor,pillar,spikes,wall}`,
  74 `material` overrides (73 × `Kit/DungeonTwoSided` and 1 × `Kit/Stone`, all
  on `kit.floor`). Zero occurrences of the substring `obj`.
- `ritual_boss_showroom.scn`: 88 entities, 9 prefabs, no material overrides.

`game/assets/schemas/scene.schema.json` has no `mesh` property at all. So D9
does not touch `.scn` — only `kit.toml` and the cooked output. Note the id
style mismatch though: `.scn` prefabs are dotted (`kit.floor`) while D14's
logical ids are slashed (`kit/floor`). They are different namespaces (prefab vs
mesh) and can legitimately differ, but say so in the docs or the next reader
will assume one is a typo.

The editor can still write an absolute mesh path into a document via
`MeshSource` — `MapPlay.cpp:72-75` documents that hazard and works around it
with an "exists on disk, else resolve" two-step. P9 should make `MeshSource`
hold a logical id and delete the workaround (Batch D, §2.4).

---

## 6. `[formats]` resolution

`assets/assets.toml:97-101` already declares it and the resolver does not read
it yet (`AssetRoot.h` has no format-aware entry point):

```toml
[formats]
mesh    = [".mesh", ".glb", ".gltf", ".obj"]
texture = [".png"]
```

P9 needs one new function, not a change to `resolve()`:

```cpp
// "kit/floor_tiles" -> <pack>/meshes/kit/floor_tiles.obj, trying [formats]
// entries in order within each mounted pack before falling through to the next.
std::filesystem::path resolveAsset(std::string_view kind,   // "mesh"
                                   std::string_view id);
```

The mount walk must be the outer loop and the format list the inner one — a
higher-priority pack's `.obj` must beat a lower pack's `.glb`, otherwise the
demo pack could no longer override a game mesh. State that explicitly; it is
the kind of ordering that gets inverted by accident.

`modelImportCacheKey` (`ModelImport.h:257`) canonicalises its first argument as
a filesystem path. Feed it the **resolved absolute path**, not the logical id,
or `weakly_canonical` will produce garbage relative to the process CWD.

---

## 7. Risk ranking

Highest first.

1. **P10 / D13 — cannot be pixel-neutral as specified.** §4.4. Needs a decision
   before any code is written, and a wider gate than `make visual-test`.
2. **P9 re-cook on a stale baseline.** §5.3. Cheap to defuse: refresh the map in
   its own commit first.
3. **`assetlint`'s `OBJ_RE` rule silently dying in P9.** §5.1. A content gate
   that stops firing is worse than one that never existed.
4. **Batch A's signature churn.** `DungeonMap::load`'s two directory parameters
   and `DemoScene::load`'s `meshDir` disappear, touching headers included by
   several translation units. Mechanical, but it is where the compile-time cost
   lands.
5. **`SceneFactory.cpp:264`'s TOML-relative path join.** §2.2. It is the only
   call site whose path base is neither a pack nor a passed-in dir.
6. **Losing `loadGeometry`'s test coverage.** §3.5. The two-stream property is
   only pinned by `ObjGeometryTests`; if that test is deleted rather than
   ported, nothing catches an importer that folds the streams together.
7. **The `ModelImportOptions` overload's only caller being dead.** §3.5. Low
   risk, but the reviewer will ask, so state it in the PR rather than be asked.

### 7.1 What a screenshot cannot see

**Collision geometry.** A mesh whose collision stream changed while its render
stream did not passes every visual gate. Today the exposure is zero, because
`spawnModel` — the only consumer — is uncalled (§3.5). But the exposure is zero
by accident, not by design, and `CLAUDE.md`'s weapon/projectile work will very
plausibly reach for `ColliderMode::StaticMesh`.

Concrete mitigation, cheap enough to do inside P7:

- Extend `engine/tests/ObjGeometryTests.cpp` into a `MeshImporterTests` that
  asserts, for one shipped kit mesh and one vertex-coloured mesh
  (`props/prop_torch.obj` is the only prop with colours), the exact
  `render.positions.size()`, `collision.vertices.size()`, `indices.size()`, and
  the first and last triangle of each. Record the numbers from the pre-P7 build
  and pin them. That is a byte-level regression gate for the half of the loader
  no camera points at.
- Keep the `appendTriangleFan` winding assertion (`ObjGeometryTests.cpp:51-53`).
  It is the only thing tying the two triangulations together.

**Material→mesh pairings that only appear in the authored map.** §4.5's wider
gate covers this; it applies to P8 batch B as well as to P10.

### 7.2 Coverage map — which app shows what

| Content | Shown by |
|---|---|
| kit walls/floors/pillars, procedural dungeon, viewmodel, DemoScene lighting | `game`, default args |
| `asset_mesh` exhibits incl. the two liquid `Floor_Tiles` | `game <map>` from `ritual_boss_showroom.scn` — **not** the default |
| cooked-map playback, `MeshSource` resolution | `game tech_demo.map` |
| editor mesh preview, placement ghost | `scene_editor` |
| `Kit/*` on a procedural sphere | `make material` |
| crystal shrine, portal surround, prop dressing | `psx_demo` |

---

## 8. Errata against `2026-07-31-unified-asset-root.md`

Collected in one place, in document order.

| Location | Says | Actually |
|---|---|---|
| §1.5, §8 | `loadObj` has **54 call sites** | **47**, in 13 files (§2) |
| §1.5 | `kit.material` undoes the flip in **4+ passes** | **7** passes, and no other `.material` file in the repo has the pair (§4.1) |
| §1.5 | **90** `"*.obj"` literals in TOML | **99**, across five files, one of which (`showroom_props.toml`) is orphaned data (§5.1) |
| §4.3 | `MeshData` = one flat vertex/index set | two streams; the collision one is source-indexed (§3.1) |
| §4.3 | `defaultMaterial` "may be empty" | always empty: zero `.mtl`, one `usemtl` in the tree (§3.4) |
| §4.3 | "the importer applies the bake" | must **derive** it for the options overload (§3.3) |
| §4.3 | "The mesh cache keys off `modelImportCacheKey()` — no change needed there" | **there is no mesh cache**; the key is stored and never read, and P9 breaks its `weakly_canonical` assumption (§3.7, §6) |
| §5 P9 | "Re-cook `tech_demo.map` and any other `.map`" | there is no other `.map`; and `tech_demo.map` is already stale by 110 bytes before P9 starts (§5.3) |
| §5 P10 | "in one commit … the net transform is identity by construction" | identity holds only for kit mesh × kit material; two exhibits pair a kit mesh with `Fantasy/Water` and `Fantasy/Lava`, whose vertex program has no UV transform at all (§4.4) |
| §5 P10 | "Gate: this one is purely `make visual-test`" | `make visual-test` runs the *procedural* level and never reaches `loadPrimitiveShowcase` — it cannot see the exhibits P10 affects (§4.5) |
| §7 risk table | risks are enumerated for renames, mounts and paths | no row for "a change invisible to every screenshot", which is what collision geometry is (§7.1) |

Two things the doc gets right that are worth confirming, because they were
checked rather than assumed:

- The `.map` really does store pack-root-relative paths, and the cooker really
  is deterministic — `CookParityTests` proves in-process and CLI cooks are
  byte-identical, and cooking twice gives the same bytes.
- `ModelImport.h` really is format-neutral and needs only `flipV` added.
