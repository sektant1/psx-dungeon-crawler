// The room tool's output, checked geometrically.
//
// The property that matters: a room built here has NO holes. Walls are a cell
// wide and sit entirely outside the boundary they face, so two perpendicular
// runs leave a thickness-square notch at each corner -- the builder's whole
// reason to exist is that it closes those, every time, without the author
// having to know they were there.

#include <editor/content/RoomBuilder.h>
#include <editor/content/SceneValidate.h>
#include "TestAssets.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace game::content;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "RoomBuilderTests: " << message << '\n';
        std::exit(1);
    }
}

static bool nearly(float a, float b, float tolerance = 1e-3f)
{
    return std::fabs(a - b) < tolerance;
}

static int countPrefab(const std::vector<Entity>& pieces, const std::string& id)
{
    return int(std::count_if(pieces.begin(), pieces.end(),
                             [&](const Entity& e) { return e.prefab == id; }));
}

// The ceiling is the floor piece again, one wall-height up, so the two have to
// be told apart by height rather than by prefab.
static int countAtGround(const std::vector<Entity>& pieces, const std::string& id)
{
    return int(std::count_if(pieces.begin(), pieces.end(), [&](const Entity& e) {
        return e.prefab == id && e.transform.position.y < 0.5f;
    }));
}

int main()
{
    game::test::mountGameAssets();
    KitCatalog catalog;
    std::string error;
    require(KitCatalog::load(game::test::asset("config/kit.toml"), catalog, error), error);
    const GridConfig grid = GridConfig::fromCatalog(catalog);
    const SceneDocument empty;

    // --- piece counts -------------------------------------------------------
    {
        RoomSpec spec;
        spec.col0 = 0; spec.row0 = 0;
        spec.col1 = 2; spec.row1 = 1; // 3 x 2 cells
        const std::vector<Entity> room = buildRoom(grid, catalog, spec, empty, error);
        require(error.empty(), error);
        require(spec.width() == 3 && spec.depth() == 2, "the rectangle is 3x2");
        require(countAtGround(room, "kit.floor") == 6, "one floor tile per cell");
        require(countPrefab(room, "kit.floor") == 12,
                "and one ceiling tile above each of them");
        // Perimeter of a 3x2: 3 north + 3 south + 2 west + 2 east.
        require(countPrefab(room, "kit.wall") == 10, "walls on the perimeter only");
        require(countPrefab(room, "kit.pillar") == 4, "a post at each corner");
    }

    // --- drag direction does not matter ------------------------------------
    {
        RoomSpec forward;
        forward.col1 = 3; forward.row1 = 3;
        RoomSpec backward;
        backward.col0 = 3; backward.row0 = 3;
        const std::size_t a = buildRoom(grid, catalog, forward, empty, error).size();
        const std::size_t b = buildRoom(grid, catalog, backward, empty, error).size();
        require(a == b && a > 0, "dragging either way builds the same room");
    }

    // --- a single cell ------------------------------------------------------
    {
        RoomSpec spec; // 1 x 1
        const std::vector<Entity> room = buildRoom(grid, catalog, spec, empty, error);
        require(countAtGround(room, "kit.floor") == 1, "one tile");
        require(countPrefab(room, "kit.wall") == 4, "walled on all four sides");
        require(countPrefab(room, "kit.pillar") == 4, "and four posts");
    }

    // --- the corner posts actually cover the notches ------------------------
    {
        RoomSpec spec;
        spec.col1 = 2; spec.row1 = 2;
        const std::vector<Entity> room = buildRoom(grid, catalog, spec, empty, error);
        const KitPiece* pillar = catalog.find("kit.pillar");
        const KitPiece* wall = catalog.find("kit.wall");
        require(pillar && wall, "the kit has both pieces");

        // The notch at the north-west outside corner of cell (0,0).
        const glm::vec3 expected = cornerPostPosition(grid, 0, 0, false, false);
        bool found = false;
        for (const Entity& piece : room) {
            if (piece.prefab != "kit.pillar")
                continue;
            if (nearly(piece.transform.position.x, expected.x) &&
                nearly(piece.transform.position.z, expected.z))
                found = true;
        }
        require(found, "a post sits on the north-west notch");

        // Wide enough to cover it: the notch is one wall thickness square.
        const float thickness = wall->sizeMeters(catalog.scale()).z;
        const float postWidth = pillar->sizeMeters(catalog.scale()).x;
        require(postWidth >= thickness,
                "the post is at least as wide as the hole it fills");

        // ...and scaled to the wall's height, not left standing proud of it.
        const float wallHeight = wall->sizeMeters(catalog.scale()).y;
        for (const Entity& piece : room) {
            if (piece.prefab != "kit.pillar")
                continue;
            const float height =
                pillar->sizeMeters(catalog.scale()).y * piece.transform.scale.y;
            require(nearly(height, wallHeight, 0.01f),
                    "a corner post is exactly wall height");
        }
    }

    // --- a built room validates clean --------------------------------------
    // The end-to-end claim: what the tool produces has no corner gaps and no
    // overlapping pieces, which is what the author would otherwise have to
    // check by eye.
    {
        SceneDocument document;
        document.id = "scene.room_test";
        Entity spawn;
        spawn.id = "spawn";
        spawn.playerSpawn = true;
        document.add(spawn);
        Entity exit;
        exit.id = "exit";
        exit.exitYawDegrees = 0.0f;
        document.add(exit);

        RoomSpec spec;
        spec.col1 = 3; spec.row1 = 2;
        for (const Entity& piece : buildRoom(grid, catalog, spec, document, error))
            document.add(piece);

        const std::vector<Issue> issues = validate(document, catalog);
        for (const Issue& issue : issues) {
            std::cerr << "  " << severityName(issue.severity) << ' ' << issue.code
                      << " (" << issue.entity << "): " << issue.message << '\n';
        }
        require(issues.empty(), "a tool-built room validates completely clean");
    }

    // --- ids are unique across a batch -------------------------------------
    {
        RoomSpec spec;
        spec.col1 = 4; spec.row1 = 4;
        const std::vector<Entity> room = buildRoom(grid, catalog, spec, empty, error);
        std::vector<std::string> ids;
        for (const Entity& piece : room)
            ids.push_back(piece.id);
        std::sort(ids.begin(), ids.end());
        require(std::adjacent_find(ids.begin(), ids.end()) == ids.end(),
                "every piece in one room gets its own id");
    }

    // --- unknown prefabs are refused, not silently skipped ------------------
    {
        RoomSpec spec;
        spec.wallPrefab = "kit.not_a_wall";
        const std::vector<Entity> room = buildRoom(grid, catalog, spec, empty, error);
        require(room.empty() && !error.empty(),
                "an unknown prefab fails loudly");
    }

    // --- the ceiling closes the room ---------------------------------------
    {
        RoomSpec spec;
        spec.col1 = 1; spec.row1 = 1;
        const std::vector<Entity> room = buildRoom(grid, catalog, spec, empty, error);
        const KitPiece* wall = catalog.find("kit.wall");
        require(wall, "the kit has a wall");
        const float wallHeight = wall->sizeMeters(catalog.scale()).y;

        int lids = 0;
        for (const Entity& piece : room) {
            if (piece.prefab != "kit.floor" || piece.transform.position.y < 0.5f)
                continue;
            ++lids;
            require(nearly(piece.transform.position.y, wallHeight),
                    "the ceiling sits exactly at wall height");
            require(!piece.material.empty(),
                    "seen from below it needs the two-sided material");
            // Its collision goes above the mesh, not below it: hung the other
            // way it is a slab across the room at head height.
            require(piece.collider && piece.collider->offset.y > 0.0f,
                    "the ceiling's collision is on top of it");
        }
        require(lids == 4, "one lid per cell");

        RoomSpec open = spec;
        open.ceiling = false;
        require(countPrefab(buildRoom(grid, catalog, open, empty, error),
                            "kit.floor") == 4,
                "a room can still be authored open to the sky");
    }

    std::cout << "RoomBuilderTests: ok\n";
    return 0;
}
