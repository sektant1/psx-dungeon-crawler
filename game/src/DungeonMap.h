#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

#include "DungeonGen.h"
#include "Targeting.h"

namespace eng {
class Renderer;
class Physics;
} // namespace eng

// Data-driven modular dungeon built from the PSX_Modular_Medieval tile set
// (game/assets/meshes/tiles, sliced by tools/slice_tiles.py).
//
// The layout lives in a TOML file as an ASCII grid; each character is one
// cell_size x cell_size cell:
//   '#'  solid rock (walls are emitted on faces adjacent to walkable cells)
//   '.'  floor
//   'A'  archway: a straight doorway cell. It must join exactly two opposite
//        walkable neighbours; unsupported ends, bends and junctions safely
//        fall back to floor so the dungeon shell remains closed.
//   'L'  floor + wall-mounted torch (fire/ash particles + warm point light)
//   'S'  floor + player spawn
//   'C'  floor + scene anchor; the map is shifted so this cell's centre is
//        the world origin (the shared DemoScene loads at fixed positions
//        around the origin)
//   'H'/'B'/'R'/'V' floor + chest/barrel/crate/urn set dressing
//   ' '  void (outside the dungeon)
//
// Rendering: floor + ceiling tile per walkable cell, wall segments facing
// inward on walkable/solid boundaries, pillars on wall corners, arches on
// 'A' cells. Collision: static box bodies emitted into Jolt physics by
// buildFromLayout; the FPS controller drives a CharacterVirtual against them.
class DungeonMap
{
public:
    bool load(eng::Renderer& r, eng::Physics& physics,
              const std::string& tomlPath,
              const std::string& tileMeshDir, const std::string& propMeshDir,
              eng::NodeHandle sceneRoot = eng::kRootNode);

    // Build the dungeon from an in-memory grid (e.g. from gen::generate)
    // instead of a TOML file. Shares all geometry/segmentation/torch code.
    bool loadFromRows(eng::Renderer& r, eng::Physics& physics,
                      gen::Layout layout,
                      const std::string& tileMeshDir,
                      const std::string& propMeshDir,
                      eng::NodeHandle sceneRoot = eng::kRootNode);

    // Remove all static collider bodies registered by the last build.
    // Call before rebuilding or destroying the map to avoid body leaks.
    void clearPhysics();

    glm::vec3 spawn() const { return mSpawn; }
    glm::vec3 exitPos() const { return mExit; }
    float exitYawDegrees() const { return mExitYawDegrees; }

    // Read-only grid data for the generated-dungeon inspector. Kept separate
    // from gameplay traversal so debug UI cannot mutate level state.
    int debugRows() const { return mLayout.rowCount(); }
    int debugColumns() const { return mLayout.columnCount(); }
    const std::vector<std::string>& debugLayoutRows() const { return mLayout.rows(); }
    char debugCellAt(int col, int row) const { return cellAt(col, row); }
    int debugRoomAt(int col, int row) const { return roomOfCell(col, row); }
    bool debugArchNorthSouth(int col, int row) const;
    void debugCellOf(glm::vec3 worldPos, int& col, int& row) const
    {
        cellOf(worldPos.x, worldPos.z, col, row);
    }

    // Torch light flicker: call once per frame with the animation clock.
    void update(eng::Renderer& r, float t) const;

    // Occlusion culling: hide rooms unreachable through visible portals within
    // farDist of the camera. Call once per frame before rendering.
    void updateVisibility(eng::Renderer& r, glm::vec3 cameraPos, float farDist);

    // Torch adapter for the shared gameplay-target seam.
    void appendTorchTargets(std::vector<GameplayTarget>& targets) const;
    bool torchLit(int index) const { return mTorches[size_t(index)].lit; }
    // Toggle flame + light together (the tip node carries both).
    void toggleTorch(eng::Renderer& r, int index);

    // Deterministic sweep retained for editor/tools and regression tests;
    // runtime locomotion uses the Jolt character controller.
    glm::vec3 resolveMove(glm::vec3 from, glm::vec3 to, float radius) const;

private:
    // Shared build core: assumes rows + cell/wall/light params are chosen.
    // Both load() (TOML) and loadFromRows() (generator) funnel through here.
    bool buildFromLayout(eng::Renderer& r, eng::Physics& physics,
                       gen::Layout layout,
                       float cell, float wallH, glm::vec3 lightColour,
                       float lightEnergy, float lightRange, float lampY,
                       const std::string& tileMeshDir,
                       const std::string& propMeshDir,
                       eng::NodeHandle sceneRoot);

    char cellAt(int col, int row) const;
    bool walkableCell(int col, int row) const;
    bool circleFits(float x, float z, float radius) const;

    struct Torch {
        eng::LightHandle light;
        eng::NodeHandle tip;  // flame seat: particles + light
        glm::vec3 tipPos;     // world position of the flame
        glm::vec3 baseColour; // linear, energy pre-multiplied
        float phase;          // de-syncs the flicker between torches
        bool lit = true;
    };

    // Static-geometry batches for room-level occlusion culling. Each room is
    // a connected group of walkable non-arch cells; each 'A' arch is its own
    // batch tagged with the (<=2) rooms it joins.
    struct Room {
        eng::StaticBatchHandle batch;
        eng::NodeHandle node;
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
    };
    struct Arch {
        eng::StaticBatchHandle batch;
        eng::NodeHandle node;
    };
    std::vector<Room> mRooms;
    std::vector<Arch> mArches;
    // Cache of the last current-room set (sort key for the visibility early-
    // out). Keyed on current rooms only: safe because every reachable room in
    // this dungeon is well within farDist, so the visible set can't change
    // without the current-room set changing.
    std::vector<int> mLastCurrentRooms;
    // Reused by the portal-visibility walk. Room crossings used to allocate
    // three vectors in one frame, producing a visible frametime tooth.
    std::vector<int> mCurrentScratch;
    std::vector<int> mQueueScratch;
    std::vector<char> mVisibleScratch;

    // World (col,row) of a ground-plane point; may be outside the grid.
    void cellOf(float x, float z, int& col, int& row) const;
    int roomOfCell(int col, int row) const; // -1 if none

    gen::Layout mLayout;
    std::vector<Torch> mTorches;
    struct PropBlocker { float minX, minZ, maxX, maxZ; };
    std::vector<PropBlocker> mPropBlockers;
    float mCell = 4.0f;
    glm::vec3 mOrigin{0.0f}; // world position of cell (0,0)'s NW corner
    glm::vec3 mSpawn{0.0f};
    glm::vec3 mExit{0.0f}; // world pos of the 'X' down-portal cell
    float mExitYawDegrees = 0.0f;

    // Physics: raw pointer to the long-lived Physics instance (owned by main).
    // Null until the first build that passes a Physics reference.
    eng::Physics* mPhysics = nullptr;
    // Static box colliders emitted for the dungeon shell (floor, ceiling, walls,
    // arch side-blocks). Freed by clearPhysics() before every rebuild.
    std::vector<eng::BodyHandle> mColliders;
};
