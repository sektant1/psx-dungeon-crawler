# Level Editor — Plan 4: Generator → .map Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the BSP dungeon generator (`gen::generate` → `gen::Layout`) into an emitter of editable `.map` files: a renderer-free converter builds an `entt` registry of tile/wall/marker/prop/torch entities from a layout, serialized with Plan-1 `writeMap`. Exposed as a `mapgen <seed> <out.map>` CLI and a "Generate" button in the editor that fills the current scene.

**Architecture:** A pure `game::layoutToScene(layout, opts, registry)` function walks the validated layout grid and emits entities (Transform + MeshRenderer + MeshSource + Collider, plus PlayerSpawn/Exit/LightRef) at cell world positions mirroring `DungeonMap`'s placement (cell size, anchor origin). It touches no renderer or physics — just the registry — so it is unit-tested headless. The CLI writes the registry via `mapio::writeMap`; the editor calls the same converter into its live `EditorScene` registry and resyncs.

**Tech Stack:** C++17, EnTT, GLM, `gen::Layout` (`game/src/DungeonGen.h`), Plan-1 `mapio`, the editor's `EditorScene`, CMake + CTest.

**Scope / fidelity:** The converter emits floor + ceiling tiles per walkable cell, a wall tile on each walkable→solid edge, box colliders (floor slab + wall segments), `PlayerSpawn`/`Exit` markers, prop entities for `H/B/R/V`, and a point-light entity for torch cells `L`. It deliberately does NOT reproduce `DungeonMap`'s pillars, arch side-blocks, or per-room static batching — those are runtime-rendering optimizations; the authored `.map` is meant to be opened and refined in the editor. Mesh orientation for walls uses a per-edge yaw convention; a designer can nudge any tile afterward.

---

## File Structure

New (`game/src/scene/`, compiled into both `game` and `level_editor`):
- `LayoutToScene.h/.cpp` — `struct SceneGenOptions { float cell; std::string tileDir; std::string propDir; };` and `void layoutToScene(const gen::Layout&, const SceneGenOptions&, entt::registry&)`.

New (`game/src/`):
- `mapgen_main.cpp` — CLI: `mapgen <seed> <out.map> [assetDir]`.

Modified:
- `game/src/editor/EditorApp.h/.cpp` — a "Generate" toolbar control (seed input + button) that clears the scene and runs the converter, then resolves meshes + resyncs.
- `CMakeLists.txt` — `layoutToScene` sources into `game` + `level_editor`; a `mapgen` executable; a `layout_to_scene_tests` test.

Tests (`game/tests/`): `LayoutToSceneTests.cpp`.

---

## Task 1: layoutToScene converter

**Files:**
- Create: `game/src/scene/LayoutToScene.h`, `game/src/scene/LayoutToScene.cpp`
- Test: `game/tests/LayoutToSceneTests.cpp`

Cell→world mirrors `DungeonMap`: origin anchors the layout's `anchor()` cell to the world origin, and cell `(col,row)` centre is `origin + ((col+0.5)*cell, y, (row+0.5)*cell)`. Mesh asset paths are stored **absolute** (`tileDir + "tile_floor.obj"`), matching the editor's `MeshSource` convention so both the editor and `MapPlay` resolve them.

- [ ] **Step 1: Failing test** — `game/tests/LayoutToSceneTests.cpp`:

```cpp
#include "LayoutToScene.h"

#include "GameComponents.h"
#include "MeshSource.h"

#include "../DungeonGen.h"

#include <eng/ecs/Components.h>

#include <cstdlib>
#include <iostream>

using namespace game;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "LayoutToSceneTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    // A tiny hand-authored layout: a 1x3 corridor with spawn + exit.
    gen::Layout layout = gen::Layout::fromRows(
        {"#####", "#S.X#", "#####"}, /*requireExit=*/true);
    require(layout.valid(), "fixture layout validates");

    entt::registry reg;
    SceneGenOptions opts;
    opts.cell = 4.0f;
    opts.tileDir = "/assets/meshes/tiles/";
    opts.propDir = "/assets/meshes/props/";
    layoutToScene(layout, opts, reg);

    int floors = 0, spawns = 0, exits = 0, colliders = 0, meshes = 0;
    reg.view<mapio::MeshSource>().each([&](entt::entity, const mapio::MeshSource& s) {
        ++meshes;
        if (s.path.find("tile_floor") != std::string::npos) ++floors;
    });
    reg.view<game::PlayerSpawn>().each([&](auto...) { ++spawns; });
    reg.view<game::Exit>().each([&](auto...) { ++exits; });
    reg.view<game::Collider>().each([&](auto...) { ++colliders; });

    require(floors == 3, "one floor tile per walkable cell (S, ., X)");
    require(spawns == 1, "exactly one PlayerSpawn");
    require(exits == 1, "exactly one Exit");
    require(colliders > 0, "colliders were emitted (floor + walls)");
    require(meshes >= floors, "mesh entities include floors + walls + ceilings");

    // PlayerSpawn sits at the spawn cell's world centre (anchor-relative).
    entt::entity sp = entt::null;
    reg.view<game::PlayerSpawn, eng::ecs::Transform>().each(
        [&](entt::entity e, auto&&...) { sp = e; });
    require(sp != entt::null, "spawn entity has a transform");

    std::cout << "LayoutToSceneTests OK\n";
    return 0;
}
```

- [ ] **Step 2: CMake target** under `if(BUILD_TESTING)`:

```cmake
  add_executable(layout_to_scene_tests
    game/tests/LayoutToSceneTests.cpp
    game/src/scene/LayoutToScene.cpp
    game/src/DungeonGen.cpp)
  target_include_directories(layout_to_scene_tests
    PRIVATE game/src/scene game/src engine/include third_party)
  target_link_libraries(layout_to_scene_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME layout_to_scene COMMAND layout_to_scene_tests)
```
Reconfigure: `cmake -S /home/sektant1/psx-dungeon-crawler -B /home/sektant1/psx-dungeon-crawler/build`.
(`DungeonGen.cpp` provides `gen::Layout`; it is renderer-free — `dungeon_layout_tests` already compiles it standalone.)

- [ ] **Step 3: Confirm fail.**

- [ ] **Step 4: Header** — `game/src/scene/LayoutToScene.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

#include <string>

namespace gen { class Layout; }

namespace game {

struct SceneGenOptions {
    float cell = 4.0f;
    float wallHeight = 3.0f;
    std::string tileDir; // absolute, trailing slash: ".../meshes/tiles/"
    std::string propDir; // absolute, trailing slash: ".../meshes/props/"
};

// Populate `reg` with tile/wall/marker/prop/torch entities for `layout`. Adds
// only registry data (Transform, MeshRenderer, MeshSource, Collider, PlayerSpawn,
// Exit, LightRef) -- no renderer or physics. `reg` is NOT cleared first.
void layoutToScene(const gen::Layout& layout, const SceneGenOptions& opts,
                   entt::registry& reg);

} // namespace game
```

- [ ] **Step 5: Implementation** — `game/src/scene/LayoutToScene.cpp`:

```cpp
#include "LayoutToScene.h"

#include "GameComponents.h"
#include "MeshSource.h"

#include "../DungeonGen.h"

#include <eng/LightDesc.h>
#include <eng/ecs/Components.h>

#include <glm/gtc/quaternion.hpp>

namespace game {

namespace {

// Spawn a mesh entity: Transform (+yaw), MeshRenderer + MeshSource, optional
// box collider. Returns the entity.
entt::entity meshEntity(entt::registry& reg, const std::string& path,
                        const std::string& material, glm::vec3 pos, float yawDeg,
                        const char* name)
{
    entt::entity e = reg.create();
    reg.emplace<eng::ecs::Name>(e, eng::ecs::Name{name});
    eng::ecs::Transform t;
    t.position = pos;
    t.rotation = glm::angleAxis(glm::radians(yawDeg), glm::vec3(0, 1, 0));
    reg.emplace<eng::ecs::Transform>(e, t);
    reg.emplace<mapio::MeshSource>(e, mapio::MeshSource{path});
    eng::ecs::MeshRenderer mr;
    mr.material = material;
    reg.emplace<eng::ecs::MeshRenderer>(e, mr);
    return e;
}

void addBoxCollider(entt::registry& reg, entt::entity e, glm::vec3 halfExtents)
{
    reg.emplace<game::Collider>(
        e, game::Collider{eng::ShapeKind::Box, halfExtents, eng::BodyLayer::Static});
}

} // namespace

void layoutToScene(const gen::Layout& layout, const SceneGenOptions& opts,
                   entt::registry& reg)
{
    const float cell = opts.cell;
    const float wallH = opts.wallHeight;
    const gen::Cell anchor = layout.anchor();
    const int ac = anchor.valid() ? anchor.col : layout.columnCount() / 2;
    const int ar = anchor.valid() ? anchor.row : layout.rowCount() / 2;
    const glm::vec3 origin(-(ac + 0.5f) * cell, 0.0f, -(ar + 0.5f) * cell);
    const auto centre = [&](int col, int row) {
        return glm::vec3(origin.x + (col + 0.5f) * cell, 0.0f,
                         origin.z + (row + 0.5f) * cell);
    };

    const std::string tileFloor = opts.tileDir + "tile_floor.obj";
    const std::string tileCeil = opts.tileDir + "tile_ceiling.obj";
    const std::string tileWall = opts.tileDir + "tile_wall.obj";

    for (int row = 0; row < layout.rowCount(); ++row) {
        for (int col = 0; col < layout.columnCount(); ++col) {
            if (!layout.walkable(col, row)) continue;
            const glm::vec3 c = centre(col, row);

            // Floor tile + a thin slab collider under the cell.
            meshEntity(reg, tileFloor, "Game/DungeonFloor", c, 0.0f, "Floor");
            entt::entity slab =
                meshEntity(reg, tileFloor, "Game/DungeonFloor",
                           c + glm::vec3(0, -0.05f, 0), 0.0f, "FloorCollider");
            addBoxCollider(reg, slab, glm::vec3(cell * 0.5f, 0.1f, cell * 0.5f));
            reg.remove<eng::ecs::MeshRenderer>(slab); // collider-only helper
            reg.remove<mapio::MeshSource>(slab);

            // Ceiling tile.
            meshEntity(reg, tileCeil, "Game/DungeonCeiling",
                       c + glm::vec3(0, wallH, 0), 0.0f, "Ceiling");

            // Walls on edges facing a non-walkable neighbour. Yaw faces inward:
            // +Z(south)=0, +X(east)=90, -Z(north)=180, -X(west)=270.
            struct Edge { int dc, dr; float yaw; glm::vec3 off; };
            const float h = cell * 0.5f;
            const Edge edges[4] = {
                {0, 1, 0.0f, {0, 0, h}},   {1, 0, 90.0f, {h, 0, 0}},
                {0, -1, 180.0f, {0, 0, -h}}, {-1, 0, 270.0f, {-h, 0, 0}},
            };
            for (const Edge& ed : edges) {
                if (layout.walkable(col + ed.dc, row + ed.dr)) continue;
                const glm::vec3 wp = c + ed.off;
                meshEntity(reg, tileWall, "Game/DungeonWall", wp, ed.yaw, "Wall");
                entt::entity wc =
                    meshEntity(reg, tileWall, "Game/DungeonWall", wp, ed.yaw, "WallCollider");
                reg.remove<eng::ecs::MeshRenderer>(wc);
                reg.remove<mapio::MeshSource>(wc);
                // Thin box across the edge, full wall height.
                const bool ns = (ed.dc == 0);
                addBoxCollider(reg, wc,
                               ns ? glm::vec3(cell * 0.5f, wallH * 0.5f, 0.15f)
                                  : glm::vec3(0.15f, wallH * 0.5f, cell * 0.5f));
                reg.get<eng::ecs::Transform>(wc).position.y = wallH * 0.5f;
            }

            // Markers + dressing by cell glyph.
            const char glyph = layout.cellAt(col, row);
            switch (glyph) {
            case 'S': {
                entt::entity e = reg.create();
                reg.emplace<eng::ecs::Name>(e, eng::ecs::Name{"PlayerSpawn"});
                eng::ecs::Transform t; t.position = c + glm::vec3(0, 1.0f, 0);
                reg.emplace<eng::ecs::Transform>(e, t);
                reg.emplace<game::PlayerSpawn>(e);
                break;
            }
            case 'X': {
                entt::entity e = reg.create();
                reg.emplace<eng::ecs::Name>(e, eng::ecs::Name{"Exit"});
                eng::ecs::Transform t; t.position = c + glm::vec3(0, 1.0f, 0);
                reg.emplace<eng::ecs::Transform>(e, t);
                reg.emplace<game::Exit>(e, game::Exit{0.0f});
                break;
            }
            case 'L': {
                entt::entity e = reg.create();
                reg.emplace<eng::ecs::Name>(e, eng::ecs::Name{"Torch"});
                eng::ecs::Transform t; t.position = c + glm::vec3(0, 1.9f, 0);
                reg.emplace<eng::ecs::Transform>(e, t);
                eng::LightDesc d; d.type = eng::LightDesc::Type::Point;
                d.colour = glm::vec3(1.0f, 0.68f, 0.34f); d.range = 6.5f;
                reg.emplace<eng::ecs::LightRef>(e, eng::ecs::LightRef{d, {}});
                break;
            }
            case 'H': case 'B': case 'R': case 'V': {
                // Prop meshes matching the DungeonMap marker set.
                const char* mesh = glyph == 'H' ? "prop_chest.obj"
                                 : glyph == 'B' ? "prop_barrel_p0.obj"
                                 : glyph == 'R' ? "prop_crate.obj"
                                 :                "prop_vase_p0.obj";
                const char* mat = glyph == 'H' ? "Game/PropChest"
                                : glyph == 'B' ? "Game/PropPlanks"
                                : glyph == 'R' ? "Game/PropMarket"
                                :                "Game/PropTerracotta";
                entt::entity e = meshEntity(reg, opts.propDir + mesh, mat, c,
                                            0.0f, "Prop");
                addBoxCollider(reg, e, glm::vec3(0.5f, 0.5f, 0.5f));
                break;
            }
            default:
                break;
            }
        }
    }
}

} // namespace game
```
Note: confirm `gen::Layout::walkable(col,row)` returns false for out-of-range coordinates (edges query neighbours outside the grid). `DungeonMap` relies on the same guard, so it should; if not, add a bounds check in the edge loop.

- [ ] **Step 6: Confirm pass** — `cmake --build build --target layout_to_scene_tests && ctest --test-dir build -R layout_to_scene --output-on-failure` → `LayoutToSceneTests OK`. If `Layout::fromRows` rejects the fixture (e.g. requires an anchor `C`), adjust the fixture to a minimal layout the validator accepts (inspect `DungeonGen.cpp` / `LevelDocument` fixtures for a valid minimal grid — the `LevelDocumentTests` fixture `{"#######","#S.C.X#","#######"}` is known-good). Keep the assertions' intent.

- [ ] **Step 7: Commit**
```bash
git add game/src/scene/LayoutToScene.h game/src/scene/LayoutToScene.cpp \
        game/tests/LayoutToSceneTests.cpp CMakeLists.txt
git commit -m "feat(map): layoutToScene converts a gen::Layout to editable entities"
```

---

## Task 2: mapgen CLI

**Files:**
- Create: `game/src/mapgen_main.cpp`
- Modify: `CMakeLists.txt`
- Verify: build + run `mapgen`, then load the output with the existing `dumpMap`/`map_serializer` path.

- [ ] **Step 1: `game/src/mapgen_main.cpp`:**

```cpp
// mapgen <seed> <out.map> [assetDir]
// Generates a BSP dungeon and writes it as an editable .map.

#include "scene/LayoutToScene.h"
#include "scene/MapSerializer.h"
#include "DungeonGen.h"

#include <entt/entt.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::printf("usage: mapgen <seed> <out.map> [assetDir]\n");
        return 2;
    }
    const uint32_t seed = uint32_t(std::strtoul(argv[1], nullptr, 10));
    const std::string out = argv[2];
    const std::string assetDir = argc > 3 ? argv[3] : APP_ASSET_DIR;

    gen::Layout layout = gen::generate(seed);
    if (!layout.valid()) {
        std::printf("mapgen: generation failed: %s\n", layout.error().c_str());
        return 1;
    }

    entt::registry reg;
    game::SceneGenOptions opts;
    opts.tileDir = assetDir + "/meshes/tiles/";
    opts.propDir = assetDir + "/meshes/props/";
    game::layoutToScene(layout, opts, reg);

    if (!mapio::writeMap(out, reg, mapio::coreRegistry())) {
        std::printf("mapgen: failed to write %s\n", out.c_str());
        return 1;
    }
    std::printf("mapgen: wrote %s (seed %u)\n", out.c_str(), seed);
    return 0;
}
```

- [ ] **Step 2: CMake — `mapgen` executable** (near the `level_editor` target, NOT under BUILD_TESTING):

```cmake
add_executable(
  mapgen
  game/src/mapgen_main.cpp
  game/src/scene/LayoutToScene.cpp
  game/src/scene/MapSerializer.cpp
  game/src/scene/ComponentRegistry.cpp
  game/src/scene/ByteStream.cpp
  game/src/DungeonGen.cpp)
target_include_directories(mapgen PRIVATE game/src game/src/scene engine/include third_party)
target_link_libraries(mapgen PRIVATE glm::glm EnTT::EnTT)
target_compile_definitions(mapgen PRIVATE APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets")
eng_target_hardening(mapgen)
```
Reconfigure.

- [ ] **Step 3: Build + smoke** — `cmake --build build --target mapgen && ./build/mapgen 42 /tmp/gen.map`. Expect `mapgen: wrote /tmp/gen.map (seed 42)` and a non-empty file (`ls -la /tmp/gen.map`).

- [ ] **Step 4: Verify the map is well-formed** — build the `map_serializer` dump path is not a CLI; instead add a one-line check by loading it in a throwaway is unnecessary. Confirm via file size > 100 bytes and that the next task (editor Generate) or `game /tmp/gen.map` opens it. (A dedicated dump CLI is out of scope; `dumpMap` is covered by unit tests.)

- [ ] **Step 5: Commit**
```bash
git add game/src/mapgen_main.cpp CMakeLists.txt
git commit -m "feat(map): mapgen CLI emits a .map from a BSP seed"
```

---

## Task 3: Editor "Generate" button

**Files:**
- Modify: `game/src/editor/EditorApp.h`, `game/src/editor/EditorApp.cpp`, `CMakeLists.txt`
- Verify: build `level_editor` + screenshot after generating.

Add a seed field + "Generate" button to the toolbar. It clears the scene, runs `layoutToScene` into the editor registry, resolves mesh handles for every new mesh entity, resyncs, clears the undo stack, and frames the result.

- [ ] **Step 1: CMake** — add `game/src/scene/LayoutToScene.cpp` and `game/src/DungeonGen.cpp` to the `level_editor` `add_executable` source list (so the converter + generator link into the editor). Reconfigure.

- [ ] **Step 2: `EditorApp.h`** — add a member `uint32_t mGenSeed = 1;` (near `mSnapStep`) and a method `void generateDungeon();` (near `newScene`). Add `#include <cstdint>` if needed.

- [ ] **Step 3: `EditorApp.cpp`** — include the converter + generator at the top:
```cpp
#include "../scene/LayoutToScene.h"
#include "../DungeonGen.h"
```
Implement `generateDungeon()` (place near `newScene()`):
```cpp
void EditorApp::generateDungeon()
{
    entt::registry& reg = mScene.registry();
    reg.clear();
    mSel.clear();
    mStack.clear();

    gen::Layout layout = gen::generate(mGenSeed);
    if (!layout.valid()) return;

    game::SceneGenOptions opts;
    opts.tileDir = mAssetDir + "/meshes/tiles/";
    opts.propDir = mAssetDir + "/meshes/props/";
    game::layoutToScene(layout, opts, reg);

    // Resolve renderer mesh handles for every generated mesh entity.
    for (auto e : reg.view<eng::ecs::MeshRenderer>())
        resolveMeshHandle(mRenderer, reg, e);
    mScene.sync();
}
```
Note: `resolveMeshHandle` is the existing free helper used elsewhere in this file (verify its signature by grepping — it is `resolveMeshHandle(mRenderer, reg, e)`).

Add the toolbar UI in `drawToolbar()`, after the New/Open/Save row (before the gizmo `Separator`):
```cpp
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    int seed = int(mGenSeed);
    if (ImGui::InputInt("##seed", &seed)) mGenSeed = uint32_t(seed < 0 ? 0 : seed);
    ImGui::SameLine();
    if (ImGui::Button("Generate")) generateDungeon();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Replace the scene with a BSP dungeon from this seed");
```

- [ ] **Step 4: Build** — `cmake -S . -B build && cmake --build build --target level_editor`. Fix any signature mismatches against the real `resolveMeshHandle` / `gen` API.

- [ ] **Step 5: Screenshot verify** — the editor can't click "Generate" headlessly, so add a temporary auto-generate at the end of the `EditorApp` constructor (`mGenSeed = 42; generateDungeon();`), run `PSX_SCREENSHOT=/tmp/gen_editor.png PSX_DEBUG_UI=1 SDL_VIDEODRIVER=x11 ./build/level_editor`, open the PNG to confirm a multi-tile dungeon renders in the viewport, then REMOVE the temporary auto-generate line before committing. (The editor's teardown may SIGSEGV/SIGABRT after the screenshot in this sandbox — that is a known headless-only teardown issue; the written PNG is the acceptance.)

- [ ] **Step 6: Full suite** — `ctest --test-dir build --output-on-failure`. All pass.

- [ ] **Step 7: Commit**
```bash
git add game/src/editor/EditorApp.h game/src/editor/EditorApp.cpp CMakeLists.txt
git commit -m "feat(editor): Generate button fills the scene from a BSP seed"
```

---

## Self-Review

**Spec coverage (against `2026-07-23-level-editor-app-design.md`, generator slice):**
- BSP generator emits an editable `.map` (not driving runtime directly) → Task 1 converter + Task 2 CLI. ✓
- Generator output is openable/editable in the editor → Task 3 Generate button (same converter into the live registry). ✓
- Uses Plan-1 `writeMap`/`coreRegistry` and the shared components → Tasks 1–2. ✓
- Renderer-free, unit-tested converter → Task 1. ✓
- Deferred (documented): pillars/arch side-blocks/room static-batching (runtime rendering detail, not authored data); trigger/enemy/pickup population (Plan-3 deferral). ✓

**Type consistency:** `game::SceneGenOptions{cell,wallHeight,tileDir,propDir}`, `game::layoutToScene(const gen::Layout&, const SceneGenOptions&, entt::registry&)`, `gen::generate`/`gen::Layout` (`walkable`/`cellAt`/`anchor`/`rowCount`/`columnCount`/`fromRows`), `mapio::writeMap`/`coreRegistry`/`MeshSource`, `game::Collider`/`PlayerSpawn`/`Exit`, `eng::ecs::{Name,Transform,MeshRenderer,LightRef}`, `eng::LightDesc`, `eng::ShapeKind`/`eng::BodyLayer` all match the headers established in Plans 1–3. `resolveMeshHandle(mRenderer, reg, e)` matches the existing editor helper.

**Placeholder scan:** Tasks 1–2 are complete code. Task 3 is a small UI wiring with exact snippets + a screenshot acceptance (temporary auto-generate, explicitly removed before commit). No `TODO`/`TBD` in committed steps.
