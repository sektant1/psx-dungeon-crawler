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

    std::vector<std::string> mRows;
    std::vector<Torch> mTorches;
    std::vector<bool> mArchNS; // per-cell: arch tunnel runs north-south
    float mCell = 4.0f;
    glm::vec3 mOrigin{0.0f}; // world position of cell (0,0)'s NW corner
    glm::vec3 mSpawn{0.0f};
};
