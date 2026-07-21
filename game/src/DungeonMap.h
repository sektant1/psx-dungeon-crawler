#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eng {
class Renderer;
} // namespace eng

// Data-driven modular dungeon built from the PSX_Modular_Medieval tile set
// (game/assets/meshes/tiles, sliced by tools/slice_tiles.py).
//
// The layout lives in a TOML file as an ASCII grid; each character is one
// cell_size x cell_size cell:
//   '#'  solid rock (walls are emitted on faces adjacent to walkable cells)
//   '.'  floor
//   'A'  archway: a doorway cell; the arch tunnel is oriented from its
//        walkable neighbours (N/S vs E/W)
//   'L'  floor + wall-mounted torch (fire/ash particles + warm point light)
//   'S'  floor + player spawn
//   'C'  floor + scene anchor; the map is shifted so this cell's centre is
//        the world origin (the shared DemoScene loads at fixed positions
//        around the origin)
//   ' '  void (outside the dungeon)
//
// Rendering: floor + ceiling tile per walkable cell, wall segments facing
// inward on walkable/solid boundaries, pillars on wall corners, arches on
// 'A' cells. Collision: resolveMove slides along the grid (axis-separated),
// with the arch's narrow opening modelled as a band across its cell.
class DungeonMap
{
public:
    bool load(eng::Renderer& r, const std::string& tomlPath,
              const std::string& tileMeshDir, const std::string& propMeshDir);

    glm::vec3 spawn() const { return mSpawn; }

    // Torch light flicker: call once per frame with the animation clock.
    void update(eng::Renderer& r, float t) const;

    // Occlusion culling: hide rooms unreachable through visible portals within
    // farDist of the camera. Call once per frame before rendering.
    void updateVisibility(eng::Renderer& r, glm::vec3 cameraPos, float farDist);

    // Interaction: index of the torch the player is looking at (within
    // maxDist of eye, roughly on the view axis), or -1.
    int findTorch(glm::vec3 eye, glm::vec3 forward, float maxDist) const;
    bool torchLit(int index) const { return mTorches[size_t(index)].lit; }
    // Toggle flame + light together (the tip node carries both).
    void toggleTorch(eng::Renderer& r, int index);

    // Slide 'from'->'to' against the walls; returns the allowed position.
    // 'radius' is the player's body radius on the ground plane.
    glm::vec3 resolveMove(glm::vec3 from, glm::vec3 to, float radius) const;

private:
    char cellAt(int col, int row) const;
    bool walkableCell(int col, int row) const;
    // Point-level test used by resolveMove's corner sampling.
    bool walkableAt(float x, float z) const;

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
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
    };
    struct Arch {
        eng::StaticBatchHandle batch;
        int roomA = -1;
        int roomB = -1;
    };
    std::vector<int> mCellRoom;   // room index per (row*mStride+col); -1 = none
    std::vector<int> mCellArch;   // arch index per cell; -1 = not an arch
    int mStride = 0;              // columns per row for mCellRoom/mCellArch
    std::vector<Room> mRooms;
    std::vector<Arch> mArches;
    std::vector<int> mVisibleRooms; // cache of last-visible room set (sorted)

    // World (col,row) of a ground-plane point; may be outside the grid.
    void cellOf(float x, float z, int& col, int& row) const;
    int roomOfCell(int col, int row) const; // -1 if none

    std::vector<std::string> mRows;
    std::vector<Torch> mTorches;
    std::vector<bool> mArchNS; // per-cell: arch tunnel runs north-south
    float mCell = 4.0f;
    glm::vec3 mOrigin{0.0f}; // world position of cell (0,0)'s NW corner
    glm::vec3 mSpawn{0.0f};
};
