#include "GridMath.h"

#include <cmath>

namespace game::content {
namespace {

struct EdgeDir {
    CellPlacement::Edge edge;
    int dx, dz;
    float yawDeg;
};

// Same table, same signs as the runtime assembler.
constexpr EdgeDir kEdges[] = {
    {CellPlacement::Edge::North, 0, -1, 0.0f},
    {CellPlacement::Edge::South, 0, 1, 180.0f},
    {CellPlacement::Edge::West, -1, 0, -90.0f},
    {CellPlacement::Edge::East, 1, 0, 90.0f},
};

const EdgeDir* findEdge(CellPlacement::Edge edge)
{
    for (const EdgeDir& dir : kEdges)
        if (dir.edge == edge) return &dir;
    return nullptr;
}

float normaliseDegrees(float degrees)
{
    float wrapped = std::fmod(degrees, 360.0f);
    if (wrapped < 0.0f) wrapped += 360.0f;
    return wrapped;
}

} // namespace

GridConfig GridConfig::fromCatalog(const KitCatalog& catalog)
{
    GridConfig grid;
    grid.cell = catalog.cellMeters();
    grid.wallInset = 2.5f * grid.cell / 20.0f;
    return grid;
}

glm::vec3 cellCentre(const GridConfig& grid, int col, int row, float level)
{
    return grid.origin + glm::vec3{(float(col) + 0.5f) * grid.cell, level,
                                   (float(row) + 0.5f) * grid.cell};
}

void edgeAnchor(const GridConfig& grid, int col, int row,
                CellPlacement::Edge edge, float level, glm::vec3& position,
                float& yawDegrees)
{
    const glm::vec3 centre = cellCentre(grid, col, row, level);
    const EdgeDir* dir = findEdge(edge);
    if (!dir) {
        position = centre;
        yawDegrees = 0.0f;
        return;
    }
    const float half = grid.cell * 0.5f;
    // Pushed one half-thickness PAST the boundary, so the slab's inner face
    // lands on the boundary instead of straddling it.
    const float out = half + grid.wallInset;
    position = centre + glm::vec3{float(dir->dx) * out, 0.0f, float(dir->dz) * out};
    yawDegrees = dir->yawDeg;
}

XformAuthor placementToTransform(const GridConfig& grid,
                                 const KitCatalog& catalog,
                                 const KitPiece& piece,
                                 const CellPlacement& placement)
{
    XformAuthor out;
    const float spinDegrees = float(placement.yawQuarters) * 90.0f;

    if (piece.socket == Socket::Wall || piece.socket == Socket::Opening) {
        float edgeYaw = 0.0f;
        edgeAnchor(grid, placement.col, placement.row, placement.edge,
                   placement.level, out.position, edgeYaw);
        out.rotationDegrees.y = normaliseDegrees(edgeYaw + spinDegrees);
    } else {
        // A span > 1 piece covers cells col..col+span-1 along its length, so
        // its centre sits between them. Which axis "length" means depends on
        // the quarter-turn: 0/2 run along X, 1/3 along Z.
        const int span = placement.span > 0 ? placement.span : 1;
        const float extra = float(span - 1) * 0.5f * grid.cell;
        out.position = cellCentre(grid, placement.col, placement.row,
                                  placement.level);
        if (placement.yawQuarters % 2 == 0)
            out.position.x += extra;
        else
            out.position.z += extra;
        out.rotationDegrees.y = normaliseDegrees(spinDegrees);
    }

    // The handful of pieces not authored with their base at Y=0 (an arch is the
    // head of an opening, a chandelier hangs from its mount).
    out.position.y += piece.yOffsetMeters(catalog.scale());
    return out;
}

bool transformToPlacement(const GridConfig& grid, const KitCatalog& catalog,
                          const KitPiece& piece, const XformAuthor& transform,
                          CellPlacement& out)
{
    CellPlacement guess;
    guess.span = piece.span;
    guess.level = 0.0f;

    glm::vec3 position = transform.position;
    position.y -= piece.yOffsetMeters(catalog.scale());
    guess.level = position.y;

    const float yaw = normaliseDegrees(transform.rotationDegrees.y);
    const int quarters = int(std::lround(yaw / 90.0f)) % 4;

    if (piece.socket == Socket::Wall || piece.socket == Socket::Opening) {
        // Undo the outward push, then the point should land on a cell centre.
        // Try every edge and keep the one whose anchor reproduces the position.
        float best = 1e9f;
        bool found = false;
        int col = 0, row = 0;
        pointToCell(grid, position, col, row);
        for (int dc = -1; dc <= 1; ++dc) {
            for (int dr = -1; dr <= 1; ++dr) {
                for (const EdgeDir& dir : kEdges) {
                    glm::vec3 anchor;
                    float anchorYaw = 0.0f;
                    edgeAnchor(grid, col + dc, row + dr, dir.edge, guess.level,
                               anchor, anchorYaw);
                    const float distance = glm::length(anchor - position);
                    if (distance < best) {
                        best = distance;
                        guess.col = col + dc;
                        guess.row = row + dr;
                        guess.edge = dir.edge;
                        guess.yawQuarters =
                            (quarters - int(std::lround(anchorYaw / 90.0f)) + 8) % 4;
                        found = true;
                    }
                }
            }
        }
        if (!found || best > grid.cell * 0.5f)
            return false;
    } else {
        guess.edge = CellPlacement::Edge::None;
        guess.yawQuarters = quarters;
        const float extra = float(guess.span - 1) * 0.5f * grid.cell;
        glm::vec3 base = position;
        if (quarters % 2 == 0)
            base.x -= extra;
        else
            base.z -= extra;
        pointToCell(grid, base, guess.col, guess.row);
        const glm::vec3 centre =
            cellCentre(grid, guess.col, guess.row, guess.level);
        if (std::fabs(centre.x - base.x) > grid.cell * 0.5f ||
            std::fabs(centre.z - base.z) > grid.cell * 0.5f)
            return false;
    }

    out = guess;
    return true;
}

void pointToCell(const GridConfig& grid, const glm::vec3& point, int& col,
                 int& row)
{
    col = int(std::floor((point.x - grid.origin.x) / grid.cell));
    row = int(std::floor((point.z - grid.origin.z) / grid.cell));
}

void nearestWallSlot(const GridConfig& grid, const glm::vec3& point, int& col,
                     int& row, CellPlacement::Edge& edge)
{
    const float cell = grid.cell;
    const glm::vec3 local = point - grid.origin;
    const float dx = local.x - std::round(local.x / cell) * cell;
    const float dz = local.z - std::round(local.z / cell) * cell;

    // ONE LINE, ONE WALL. The slot is a property of the grid line alone, never
    // of which side the cursor happens to be on.
    //
    // The alternative -- give the cell you are pointing into its own outward
    // wall -- is what the runtime does for two rooms sharing a boundary, and it
    // is wrong for hand placement: crossing the line by a centimetre would then
    // move the piece a whole wall thickness, so every wall needed nudging after
    // it was dropped. Fixing the side makes pointing anywhere near a line give
    // the identical wall, which is the entire point of snapping.
    //
    // R flips the piece for the cases where the other room's wall is what was
    // actually wanted.
    if (std::fabs(dx) <= std::fabs(dz)) {
        // Vertical line: the west edge of the cell east of it.
        col = int(std::lround(local.x / cell));
        row = int(std::floor(local.z / cell));
        edge = CellPlacement::Edge::West;
    } else {
        // Horizontal line: the north edge of the cell south of it.
        col = int(std::floor(local.x / cell));
        row = int(std::lround(local.z / cell));
        edge = CellPlacement::Edge::North;
    }
}

CellPlacement::Edge nearestEdge(const GridConfig& grid, const glm::vec3& point,
                                int col, int row)
{
    const glm::vec3 centre = cellCentre(grid, col, row, point.y);
    const float dx = point.x - centre.x;
    const float dz = point.z - centre.z;
    if (std::fabs(dx) > std::fabs(dz))
        return dx > 0.0f ? CellPlacement::Edge::East : CellPlacement::Edge::West;
    return dz > 0.0f ? CellPlacement::Edge::South : CellPlacement::Edge::North;
}

} // namespace game::content
