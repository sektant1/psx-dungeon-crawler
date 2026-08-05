// The placement rule, pinned.
//
// Height used to come from the work plane and nowhere else, which made "put
// this barrel on that table" a matter of raising a plane and guessing. It comes
// from whatever the cursor is over now, and the two cases differ on purpose:
// architecture stacks on top of what was pointed at, dressing lands exactly
// where the cursor touched. Those two sentences are what this file protects.

#include <editor/scene/BrushPlacement.h>
#include "TestAssets.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;
using namespace game::content;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorPlacementTests: " << message << '\n';
        std::exit(1);
    }
}

static bool nearly(float a, float b, float tolerance = 1e-3f)
{
    return std::fabs(a - b) < tolerance;
}

// A hit on the top of a box whose top is at `topY`, under the cursor.
static DocumentHit surfaceAt(glm::vec3 point, float topY)
{
    DocumentHit hit;
    hit.valid = true;
    hit.id = "under_cursor";
    hit.point = point;
    hit.normal = {0.0f, 1.0f, 0.0f};
    hit.boundsMin = {point.x - 1.0f, topY - 2.0f, point.z - 1.0f};
    hit.boundsMax = {point.x + 1.0f, topY, point.z + 1.0f};
    return hit;
}

// Straight down at (x, z): the query the editor builds when the cursor is over
// open floor and the camera is looking at it.
static PlacementQuery queryAt(float x, float z, float workPlane = 0.0f)
{
    PlacementQuery query;
    query.ray.origin = {x, 30.0f, z};
    query.ray.dir = {0.0f, -1.0f, 0.0f};
    query.workPlaneLevel = workPlane;
    query.snapXZ = true;
    query.step = 1.0f;
    return query;
}

int main()
{
    game::test::mountGameAssets();
    KitCatalog catalog;
    std::string error;
    require(KitCatalog::load(game::test::asset("config/kit.toml"), catalog,
                             error),
            error);
    const GridConfig grid = GridConfig::fromCatalog(catalog);

    Brush floorBrush;
    floorBrush.prefab = "kit.floor";
    Brush wallBrush;
    wallBrush.prefab = "kit.wall";
    Brush propBrush;
    propBrush.prefab = "kit.pillar";
    Brush lightBrush;
    lightBrush.kind = Brush::Kind::Gameplay;
    lightBrush.gameplay = Gameplay::PointLight;

    require(catalog.find("kit.floor") && catalog.find("kit.wall") &&
                catalog.find("kit.pillar"),
            "the kit still has the pieces these tests place");
    require(socketUsesGrid(catalog.find("kit.floor")->socket),
            "a floor is grid-socketed");
    require(!socketUsesGrid(catalog.find("kit.pillar")->socket),
            "a pillar is a free prop");

    // --- an empty brush places nothing -------------------------------------
    {
        const Placement out =
            resolvePlacement(grid, catalog, Brush{}, queryAt(0.0f, 0.0f));
        require(!out.valid, "a brush with no prefab resolves to nothing");
    }
    {
        Brush unknown;
        unknown.prefab = "kit.not_a_real_piece";
        const Placement out =
            resolvePlacement(grid, catalog, unknown, queryAt(0.0f, 0.0f));
        require(!out.valid, "and neither does one naming a missing piece");
    }

    // --- no geometry: the work plane, exactly as before --------------------
    {
        PlacementQuery query = queryAt(6.0f, 6.0f, 8.0f);
        const Placement out = resolvePlacement(grid, catalog, floorBrush, query);
        require(out.valid, "a floor over open space still places");
        require(!out.onSurface, "and reports that it is not on a surface");
        require(nearly(out.cell.level, 8.0f),
                "taking its height from the work plane");
    }

    // --- geometry under the cursor: architecture stacks on top -------------
    {
        PlacementQuery query = queryAt(6.0f, 6.0f);
        // Cursor touched the box partway down its side, but the top is at 3 m.
        query.surface = surfaceAt({6.0f, 1.75f, 6.0f}, 3.0f);
        const Placement out = resolvePlacement(grid, catalog, floorBrush, query);
        require(out.valid && out.onSurface, "the floor lands on the surface");
        require(nearly(out.cell.level, 3.0f),
                "at the TOP of what was pointed at, not where the cursor "
                "touched -- pointing at a wall's face must still stack the "
                "next piece squarely on it");
    }

    // --- geometry under the cursor: dressing lands where you point ---------
    {
        PlacementQuery query = queryAt(6.0f, 6.0f);
        query.surface = surfaceAt({6.0f, 1.75f, 6.0f}, 3.0f);
        const Placement out = resolvePlacement(grid, catalog, propBrush, query);
        require(out.valid && out.onSurface, "the prop lands on the surface");
        const float yOffset =
            catalog.find("kit.pillar")->yOffsetMeters(catalog.scale());
        require(nearly(out.transform.position.y, 1.75f + yOffset),
                "exactly where the cursor touched, so a torch goes where it is "
                "pointed on a wall face");
    }

    // --- Ctrl overrides the surface ----------------------------------------
    {
        PlacementQuery query = queryAt(6.0f, 6.0f, 8.0f);
        query.surface = surfaceAt({6.0f, 1.75f, 6.0f}, 3.0f);
        query.forceWorkPlane = true;
        const Placement out = resolvePlacement(grid, catalog, floorBrush, query);
        require(out.valid, "the placement still resolves");
        require(!out.onSurface, "but ignores the geometry");
        require(nearly(out.cell.level, 8.0f),
                "and uses the work plane, which is the escape hatch for "
                "placing something inside what is already there");
    }

    // --- XZ still comes from the grid, on a surface or off it --------------
    {
        PlacementQuery flat = queryAt(6.3f, 6.3f);
        PlacementQuery raised = queryAt(6.3f, 6.3f);
        raised.surface = surfaceAt({6.3f, 2.0f, 6.3f}, 2.0f);

        const Placement a = resolvePlacement(grid, catalog, floorBrush, flat);
        const Placement b = resolvePlacement(grid, catalog, floorBrush, raised);
        require(a.cell.col == b.cell.col && a.cell.row == b.cell.row,
                "the surface decides height, never footprint -- both land in "
                "the same cell");
        require(!nearly(a.cell.level, b.cell.level),
                "and only the height differs");
    }

    // --- rotation reaches both representations -----------------------------
    //
    // yawQuarters was dead state: fed into the cell and the transform, and
    // written by nothing. Both paths are checked because a grid piece stores
    // its rotation on the cell and a prop stores degrees on the transform.
    {
        Brush turned = floorBrush;
        turned.yawQuarters = 3;
        const Placement out =
            resolvePlacement(grid, catalog, turned, queryAt(6.0f, 6.0f));
        require(out.cell.yawQuarters == 3,
                "a grid piece carries its quarter turns on the cell");

        Brush turnedProp = propBrush;
        turnedProp.yawQuarters = 1;
        const Placement prop =
            resolvePlacement(grid, catalog, turnedProp, queryAt(6.0f, 6.0f));
        require(nearly(prop.transform.rotationDegrees.y, 90.0f),
                "and a prop carries them as degrees on the transform");
    }
    {
        Brush brush;
        brush.rotate(1);
        require(brush.yawQuarters == 1, "rotate steps a quarter turn");
        brush.rotate(-2);
        require(brush.yawQuarters == 3, "and wraps backwards past zero");
        brush.rotate(1);
        require(brush.yawQuarters == 0, "and forwards past three");
    }

    // --- walls still snap to the nearest grid line -------------------------
    //
    // The surface rule must not have disturbed this: it is what keeps the ghost
    // from flipping edges as the cursor wanders down a wall.
    {
        const Placement out =
            resolvePlacement(grid, catalog, wallBrush, queryAt(6.0f, 6.0f));
        require(out.valid, "a wall places");
        int col = 0, row = 0;
        CellPlacement::Edge edge{};
        nearestWallSlot(grid, {6.0f, 0.0f, 6.0f}, col, row, edge);
        require(out.cell.col == col && out.cell.row == row &&
                    out.cell.edge == edge,
                "on the slot nearestWallSlot chooses");
    }

    // --- a gameplay brush needs no kit piece -------------------------------
    {
        PlacementQuery query = queryAt(6.0f, 6.0f);
        query.surface = surfaceAt({6.0f, 2.5f, 6.0f}, 2.5f);
        const Placement out = resolvePlacement(grid, catalog, lightBrush, query);
        require(out.valid, "a light places without a mesh to name");
        require(out.onSurface && nearly(out.transform.position.y, 2.5f),
                "landing where the cursor touched, like any other dressing");
    }

    // --- Y is never snapped to the grid step -------------------------------
    {
        PlacementQuery query = queryAt(6.0f, 6.0f);
        query.step = 4.0f;
        query.surface = surfaceAt({6.0f, 1.3f, 6.0f}, 1.3f);
        const Placement out = resolvePlacement(grid, catalog, lightBrush, query);
        require(nearly(out.transform.position.y, 1.3f),
                "rounding the height to the step is how a barrel ends up "
                "hovering over the table it was dropped on");
    }

    // --- the brush's own size reaches the placement ------------------------
    //
    // The ghost is drawn at this scale, so the placed entity has to land at it
    // too, or the preview is a lie about the thing it is previewing. Both
    // representations again: a grid piece and a free prop.
    {
        Brush big = floorBrush;
        big.scale = 2.5f;
        const Placement out =
            resolvePlacement(grid, catalog, big, queryAt(6.0f, 6.0f));
        require(nearly(out.transform.scale.x, 2.5f) &&
                    nearly(out.transform.scale.y, 2.5f) &&
                    nearly(out.transform.scale.z, 2.5f),
                "a grid piece lands at the brush's scale");

        Brush bigProp = propBrush;
        bigProp.scale = 0.5f;
        const Placement prop =
            resolvePlacement(grid, catalog, bigProp, queryAt(6.0f, 6.0f));
        require(nearly(prop.transform.scale.x, 0.5f),
                "and so does a free prop");
    }
    {
        // The pivot lift is a distance in the mesh's own frame, so it scales
        // with the mesh: a centre-authored barrel at 2x needs twice the lift or
        // it stands half through the floor it was dropped on.
        PlacementQuery query = queryAt(6.0f, 6.0f);
        query.surface = surfaceAt({6.0f, 1.0f, 6.0f}, 1.0f);
        query.baseOffset = 0.4f;
        Brush mesh;
        mesh.kind = Brush::Kind::Mesh;
        mesh.meshPath = "meshes/props/prop_malenia.obj";
        mesh.scale = 3.0f;
        const Placement out = resolvePlacement(grid, catalog, mesh, query);
        require(nearly(out.transform.position.y, 1.0f + 0.4f * 3.0f),
                "the base lift scales with the brush");
    }
    {
        Brush brush;
        brush.resize(2.0f);
        require(nearly(brush.scale, 2.0f), "resize multiplies");
        brush.resize(0.5f);
        require(nearly(brush.scale, 1.0f), "and is reversible");
        for (int i = 0; i < 200; ++i)
            brush.resize(1.5f);
        require(brush.scale <= Brush::kMaxScale,
                "and clamps, so a wheel that overshot needs no gizmo to "
                "recover from");
        for (int i = 0; i < 400; ++i)
            brush.resize(0.5f);
        require(brush.scale >= Brush::kMinScale, "at both ends");
        brush.resize(0.0f);
        require(brush.scale >= Brush::kMinScale,
                "and a zero factor is refused rather than collapsing the brush");
    }

    std::cout << "EditorPlacementTests: ok\n";
    return 0;
}
