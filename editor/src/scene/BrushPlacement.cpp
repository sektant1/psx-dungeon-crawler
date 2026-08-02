#include <editor/scene/BrushPlacement.h>

#include <cmath>

namespace ed {

using namespace game::content;

namespace {

// The height a placement should take from a hit, given what is being placed.
// See the comment on resolvePlacement for why these differ.
float surfaceLevel(const DocumentHit& hit, bool stacks)
{
    return stacks ? hit.boundsMax.y : hit.point.y;
}

} // namespace

Placement resolvePlacement(const GridConfig& grid, const KitCatalog& catalog,
                           const Brush& brush, const PlacementQuery& query)
{
    Placement out;

    // A kit brush needs its piece; a gameplay brush has none and is always
    // placeable.
    const KitPiece* piece = nullptr;
    if (brush.kind == Brush::Kind::Piece) {
        if (brush.prefab.empty())
            return out;
        piece = catalog.find(brush.prefab);
        if (!piece)
            return out;
    }

    const bool grids = piece && socketUsesGrid(piece->socket);
    const bool useSurface = query.surface.valid && !query.forceWorkPlane;
    // Architecture stacks; dressing lands where it was pointed.
    const float level = useSurface ? surfaceLevel(query.surface, grids)
                                   : query.workPlaneLevel;

    // XZ always comes from a horizontal plane at the resolved height, so the
    // footprint stays on the grid whether the height came from geometry or not.
    //
    // Always resolves to a point: a ghost that vanishes whenever the camera
    // tips towards the horizon is the single most confusing thing about a
    // placement tool, and the ray misses the plane constantly.
    glm::vec3 hit = useSurface
                        ? query.surface.point
                        : workPlanePoint(query.ray, level, query.fallbackDistance);
    hit.y = level;

    out.valid = true;
    out.onSurface = useSurface;
    out.cell = CellPlacement{};
    out.cell.level = level;
    out.cell.yawQuarters = brush.yawQuarters;

    if (grids) {
        out.cell.span = piece->span;
        if (piece->socket == Socket::Wall || piece->socket == Socket::Opening) {
            // Snapped to the nearest grid LINE, so the ghost stays put along
            // the length of a wall instead of flipping edges mid-stroke.
            nearestWallSlot(grid, hit, out.cell.col, out.cell.row,
                            out.cell.edge);
        }
        else {
            pointToCell(grid, hit, out.cell.col, out.cell.row);
        }
        out.transform = placementToTransform(grid, catalog, *piece, out.cell);
        return out;
    }

    // Props and gameplay entities are free, so the grid subdivision is only an
    // aid here. Y is never snapped: the height was taken from a surface
    // precisely so the thing would rest on it, and rounding that to the step is
    // how a barrel ends up hovering half a metre over the table.
    if (query.snapXZ && query.step > 0.0f) {
        hit.x = std::round(hit.x / query.step) * query.step;
        hit.z = std::round(hit.z / query.step) * query.step;
    }
    out.transform = XformAuthor{};
    out.transform.position = hit;
    if (piece)
        out.transform.position.y += piece->yOffsetMeters(catalog.scale());
    out.transform.rotationDegrees.y = brush.yawDegrees();
    return out;
}

} // namespace ed
