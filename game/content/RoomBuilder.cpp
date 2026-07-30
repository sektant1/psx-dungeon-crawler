#include "RoomBuilder.h"

#include <cmath>

namespace game::content {

float scaleToHeight(const KitPiece& piece, float scale, float metres)
{
    const float authored = piece.sizeMeters(scale).y;
    if (!(authored > 0.001f) || !(metres > 0.0f))
        return 1.0f;
    return metres / authored;
}

glm::vec3 cornerPostPosition(const GridConfig& grid, int col, int row, bool east,
                             bool south)
{
    // The walls of a room occupy a band one thickness wide OUTSIDE the room's
    // boundary. The post belongs in the square where those two bands would
    // cross -- diagonally out from the corner cell, half a thickness past the
    // boundary on both axes.
    const glm::vec3 centre = cellCentre(grid, col, row, 0.0f);
    const float half = grid.cell * 0.5f;
    const float out = half + grid.wallInset;
    return {centre.x + (east ? out : -out), 0.0f,
            centre.z + (south ? out : -out)};
}

std::vector<Entity> buildRoom(const GridConfig& grid, const KitCatalog& catalog,
                              const RoomSpec& spec, const SceneDocument& document,
                              std::string& error)
{
    error.clear();
    std::vector<Entity> out;

    const KitPiece* floorPiece = catalog.find(spec.floorPrefab);
    const KitPiece* wallPiece = catalog.find(spec.wallPrefab);
    const KitPiece* cornerPiece = catalog.find(spec.cornerPrefab);
    if (spec.floor && !floorPiece) {
        error = "unknown floor prefab '" + spec.floorPrefab + "'";
        return out;
    }
    if (spec.walls && !wallPiece) {
        error = "unknown wall prefab '" + spec.wallPrefab + "'";
        return out;
    }
    if (spec.corners && spec.walls && !cornerPiece) {
        error = "unknown corner prefab '" + spec.cornerPrefab + "'";
        return out;
    }

    // Ids are allocated against a working copy so a room's own pieces cannot
    // collide with each other before any of them is in the document.
    SceneDocument probe = document;
    const auto emit = [&](Entity entity) {
        entity.id = probe.allocateId(entity.prefab);
        entity.name = entity.id;
        probe.add(entity);
        out.push_back(entity);
    };

    const int c0 = spec.minCol(), c1 = spec.maxCol();
    const int r0 = spec.minRow(), r1 = spec.maxRow();

    if (spec.floor) {
        for (int row = r0; row <= r1; ++row) {
            for (int col = c0; col <= c1; ++col) {
                Entity tile;
                tile.prefab = spec.floorPrefab;
                tile.cell = CellPlacement{col, row, CellPlacement::Edge::None,
                                          1, 0, spec.level};
                tile.transform = placementToTransform(grid, catalog, *floorPiece,
                                                      *tile.cell);
                emit(std::move(tile));
            }
        }
    }

    if (spec.walls) {
        // Only the perimeter edges: an interior edge would put a wall through
        // the middle of the room.
        const auto wall = [&](int col, int row, CellPlacement::Edge edge) {
            Entity piece;
            piece.prefab = spec.wallPrefab;
            piece.cell = CellPlacement{col, row, edge, 1, 0, spec.level};
            piece.transform =
                placementToTransform(grid, catalog, *wallPiece, *piece.cell);
            emit(std::move(piece));
        };
        for (int col = c0; col <= c1; ++col) {
            wall(col, r0, CellPlacement::Edge::North);
            wall(col, r1, CellPlacement::Edge::South);
        }
        for (int row = r0; row <= r1; ++row) {
            wall(c0, row, CellPlacement::Edge::West);
            wall(c1, row, CellPlacement::Edge::East);
        }

        if (spec.corners) {
            // Scaled to the wall's height rather than left at its authored
            // size: the kit's pillar is half again as tall as a wall, and a
            // post standing proud of the wall line reads as a mistake in a
            // plain room even though it is fine as deliberate architecture.
            const float wallHeight = wallPiece->sizeMeters(catalog.scale()).y;
            const float postScale =
                scaleToHeight(*cornerPiece, catalog.scale(), wallHeight);
            const struct { int col, row; bool east, south; } corners[4] = {
                {c0, r0, false, false},
                {c1, r0, true, false},
                {c0, r1, false, true},
                {c1, r1, true, true},
            };
            for (const auto& corner : corners) {
                Entity post;
                post.prefab = spec.cornerPrefab;
                post.transform.position =
                    cornerPostPosition(grid, corner.col, corner.row, corner.east,
                                       corner.south);
                post.transform.position.y = spec.level;
                post.transform.scale.y = postScale;
                emit(std::move(post));
            }
        }
    }

    return out;
}

} // namespace game::content
