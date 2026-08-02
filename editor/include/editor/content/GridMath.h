#pragma once
#include <editor/content/KitCatalog.h>
#include <editor/content/SceneDocument.h>

#include <glm/glm.hpp>

namespace game::content {

// The dungeon's grid, in metres. Derived from the kit rather than configured
// twice: the cell is kit.toml's cell_size at kit.toml's scale.
struct GridConfig {
    float cell = 4.0f;
    // A wall slab is 5 kit units thick, so it sits half its thickness OUTSIDE
    // the cell boundary it faces -- otherwise it straddles the boundary and
    // eats into the room. 2.5/20 * cell = 0.5 m at a 4 m cell. This is the one
    // number that must match game/src/scene/LayoutToScene.cpp:101, and the
    // grid_math test compares them.
    float wallInset = 0.5f;
    glm::vec3 origin{0.0f}; // world position of cell (0,0)'s corner

    static GridConfig fromCatalog(const KitCatalog& catalog);
};

// Centre of a cell, on the given work-plane height.
glm::vec3 cellCentre(const GridConfig& grid, int col, int row, float level);

// Where a piece standing on one edge of a cell goes, and which way it faces.
// The yaw table matches the runtime's: north 0, east 90, south 180, west -90.
void edgeAnchor(const GridConfig& grid, int col, int row,
                CellPlacement::Edge edge, float level, glm::vec3& position,
                float& yawDegrees);

// Authored cell placement -> the absolute transform the .scn stores and the
// cooker consumes. THE only function that knows about the wall inset, the
// y_offset exceptions and how span shifts a piece's centre.
//
// Scale is deliberately not set here: the kit import scale is applied once by
// the cooker, so a transform's scale stays the author's own multiplier.
XformAuthor placementToTransform(const GridConfig& grid,
                                 const KitCatalog& catalog,
                                 const KitPiece& piece,
                                 const CellPlacement& placement);

// Best-effort inverse, for importing scenes authored before `cell` existed and
// for snapping a freely-moved piece back onto the grid. Ambiguous by nature --
// which is exactly why the editor stores `cell` instead of relying on this.
bool transformToPlacement(const GridConfig& grid, const KitCatalog& catalog,
                          const KitPiece& piece, const XformAuthor& transform,
                          CellPlacement& out);

// Which cell a world point falls in, for the placement tool's cursor.
void pointToCell(const GridConfig& grid, const glm::vec3& point, int& col,
                 int& row);
// Nearest cell edge to a world point, for placing walls by pointing near one.
CellPlacement::Edge nearestEdge(const GridConfig& grid, const glm::vec3& point,
                                int col, int row);

// Which wall slot a point is closest to, snapped to the nearest GRID LINE
// rather than to a quadrant of the cell under the cursor.
//
// The difference matters for hand placement. Choosing the edge by quadrant
// means the answer flips as the cursor wanders across the middle of a cell, so
// running along a wall makes the ghost jump between edges and the author ends
// up nudging pieces after the fact. Snapping to the line the cursor is nearest
// keeps the ghost on that line for the whole length of it: point roughly at a
// wall, get that wall, every time.
//
// The wall always lands on the far side of the line from the cursor -- you are
// standing in the room, the wall goes on the outside of it.
void nearestWallSlot(const GridConfig& grid, const glm::vec3& point, int& col,
                     int& row, CellPlacement::Edge& edge);

} // namespace game::content
