// The editor and the runtime must agree on where a wall goes, down to the
// 0.5 m inset. This test pins the editor's placement maths against the
// runtime's own formula (game/src/scene/LayoutToScene.cpp), reproduced here on
// purpose: if either side changes, the two stop matching and this fails.

#include "GridMath.h"
#include "KitCatalog.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace game::content;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "GridMathTests: " << message << '\n';
        std::exit(1);
    }
}

static bool nearly(float a, float b, float tolerance = 1e-3f)
{
    return std::fabs(a - b) < tolerance;
}

static bool nearly(const glm::vec3& a, const glm::vec3& b)
{
    return nearly(a.x, b.x) && nearly(a.y, b.y) && nearly(a.z, b.z);
}

int main()
{
    KitCatalog catalog;
    std::string error;
    require(KitCatalog::load(KIT_TOML, catalog, error), error);
    const GridConfig grid = GridConfig::fromCatalog(catalog);

    require(nearly(grid.cell, 4.0f), "the grid cell is 4 m");
    // LayoutToScene.cpp:101 -- wallInset = 2.5f * cell / 20.f
    require(nearly(grid.wallInset, 2.5f * grid.cell / 20.0f),
            "the wall inset matches the runtime's");
    require(nearly(grid.wallInset, 0.5f), "which is 0.5 m at a 4 m cell");

    // --- cell centres match the runtime ------------------------------------
    for (int col = -2; col <= 3; ++col) {
        for (int row = -2; row <= 3; ++row) {
            // LayoutToScene.cpp:121 -- centre = origin + ((col+0.5)*cell, 0,
            // (row+0.5)*cell)
            const glm::vec3 expected{(float(col) + 0.5f) * grid.cell, 0.0f,
                                     (float(row) + 0.5f) * grid.cell};
            require(nearly(cellCentre(grid, col, row, 0.0f), expected),
                    "cell centre matches the runtime formula");
        }
    }

    // --- wall anchors match the runtime ------------------------------------
    // LayoutToScene.cpp:152-170: for each exposed edge (dx,dz,yaw) the mesh
    // goes to centre + dir * (halfCell + inset) with that yaw.
    struct Expect {
        CellPlacement::Edge edge;
        int dx, dz;
        float yaw;
    };
    const Expect expected[] = {
        {CellPlacement::Edge::North, 0, -1, 0.0f},
        {CellPlacement::Edge::South, 0, 1, 180.0f},
        {CellPlacement::Edge::West, -1, 0, -90.0f},
        {CellPlacement::Edge::East, 1, 0, 90.0f},
    };
    const KitPiece* wall = catalog.find("kit.wall");
    require(wall != nullptr, "kit.wall resolves");

    for (const Expect& e : expected) {
        const int col = 2, row = 3;
        const glm::vec3 centre = cellCentre(grid, col, row, 0.0f);
        const float out = grid.cell * 0.5f + grid.wallInset;
        const glm::vec3 runtimePos =
            centre + glm::vec3{float(e.dx) * out, 0.0f, float(e.dz) * out};

        CellPlacement placement;
        placement.col = col;
        placement.row = row;
        placement.edge = e.edge;
        const XformAuthor transform =
            placementToTransform(grid, catalog, *wall, placement);
        require(nearly(transform.position, runtimePos),
                "wall position matches the runtime for its edge");

        float yaw = e.yaw;
        if (yaw < 0.0f) yaw += 360.0f;
        require(nearly(transform.rotationDegrees.y, yaw),
                "wall yaw matches the runtime for its edge");
    }

    // The inset pushes the wall OUTWARD: its inner face lands exactly on the
    // cell boundary. Getting this sign wrong makes walls eat the room, which is
    // the bug the comment in LayoutToScene warns about.
    {
        CellPlacement north;
        north.col = 0;
        north.row = 0;
        north.edge = CellPlacement::Edge::North;
        const XformAuthor transform =
            placementToTransform(grid, catalog, *wall, north);
        const float boundaryZ = 0.0f; // north boundary of cell (0,0)
        const float halfThickness =
            wall->sizeMeters(catalog.scale()).z * 0.5f; // 5 kit units -> 0.5 m
        require(transform.position.z < boundaryZ,
                "the north wall sits outside the cell");
        require(nearly(transform.position.z + halfThickness, boundaryZ),
                "and its inner face lands on the boundary");
    }

    // --- round trip ---------------------------------------------------------
    for (const Expect& e : expected) {
        for (int quarters = 0; quarters < 4; ++quarters) {
            CellPlacement placement;
            placement.col = -3;
            placement.row = 5;
            placement.edge = e.edge;
            placement.yawQuarters = quarters;
            const XformAuthor transform =
                placementToTransform(grid, catalog, *wall, placement);
            CellPlacement back;
            require(transformToPlacement(grid, catalog, *wall, transform, back),
                    "a wall transform maps back to a cell");
            require(back.col == placement.col && back.row == placement.row,
                    "round trip keeps the cell");
            require(back.edge == placement.edge, "round trip keeps the edge");
            require(back.yawQuarters == placement.yawQuarters,
                    "round trip keeps the rotation");
        }
    }

    // --- span ---------------------------------------------------------------
    const KitPiece* hexagon = catalog.find("kit.floor_hexagon");
    require(hexagon && hexagon->span == 2, "floor_hexagon spans two cells");
    {
        CellPlacement placement;
        placement.col = 0;
        placement.row = 0;
        placement.span = 2;
        const XformAuthor along =
            placementToTransform(grid, catalog, *hexagon, placement);
        // Two cells wide starting at (0,0): centre sits on the shared boundary.
        require(nearly(along.position.x, grid.cell),
                "a span-2 piece centres between its two cells");
        require(nearly(along.position.z, grid.cell * 0.5f),
                "and stays centred across");

        placement.yawQuarters = 1; // turned: length now runs along Z
        const XformAuthor turned =
            placementToTransform(grid, catalog, *hexagon, placement);
        require(nearly(turned.position.x, grid.cell * 0.5f) &&
                    nearly(turned.position.z, grid.cell),
                "a quarter turn swaps which axis the span runs along");

        CellPlacement back;
        require(transformToPlacement(grid, catalog, *hexagon, turned, back),
                "a span-2 transform maps back");
        require(back.col == 0 && back.row == 0 && back.yawQuarters == 1,
                "round trip keeps a spanning placement");
    }

    // --- y_offset ----------------------------------------------------------
    const KitPiece* chandelier = catalog.find("kit.chandelier");
    require(chandelier != nullptr, "kit.chandelier resolves");
    {
        CellPlacement placement;
        placement.level = 4.0f; // hung from a ceiling one cell up
        const XformAuthor transform =
            placementToTransform(grid, catalog, *chandelier, placement);
        require(nearly(transform.position.y,
                       4.0f + chandelier->yOffsetMeters(catalog.scale())),
                "y_offset rides on top of the work-plane height");
        CellPlacement back;
        require(transformToPlacement(grid, catalog, *chandelier, transform, back),
                "and inverts cleanly");
        require(nearly(back.level, 4.0f), "round trip keeps the level");
    }

    // --- cursor helpers -----------------------------------------------------
    {
        int col = 0, row = 0;
        pointToCell(grid, {5.0f, 0.0f, -1.0f}, col, row);
        require(col == 1 && row == -1, "a point maps to the cell it is inside");
        pointToCell(grid, {-0.1f, 0.0f, 0.1f}, col, row);
        require(col == -1 && row == 0, "including on the negative side of zero");

        // Near the north boundary of cell (0,0) -> north edge.
        require(nearestEdge(grid, {2.0f, 0.0f, 0.3f}, 0, 0) ==
                    CellPlacement::Edge::North,
                "the nearest edge is the one the cursor is closest to");
        require(nearestEdge(grid, {3.9f, 0.0f, 2.0f}, 0, 0) ==
                    CellPlacement::Edge::East,
                "east too");
    }

    std::cout << "GridMathTests: ok\n";
    return 0;
}
