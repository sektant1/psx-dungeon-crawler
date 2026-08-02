// The kit catalogue is the vocabulary the editor places and the cooker
// resolves, so these assertions are against the REAL assets/game/kit.toml, not
// a fixture: if an artist renames a piece or changes the cell size, this test
// is where the build says so.

#include <editor/content/KitCatalog.h>
#include "TestAssets.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace game::content;

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "KitCatalogTests: " << message << '\n';
        std::exit(1);
    }
}

static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main()
{
    game::test::mountGameAssets();
    KitCatalog catalog;
    std::string error;
    require(KitCatalog::load(game::test::asset("config/kit.toml"), catalog, error),
            ("kit.toml loads: " + error).c_str());

    // The unit contract every placement depends on: 20 kit units at 0.2 = 4 m.
    require(nearly(catalog.scale(), 0.2f), "scale is 0.2");
    require(nearly(catalog.cellSizeKit(), 20.0f), "cell_size is 20 kit units");
    require(nearly(catalog.cellMeters(), 4.0f), "a cell is 4 metres");
    require(catalog.meshDir() == "meshes/kit", "mesh_dir is meshes/kit");
    require(catalog.all().size() >= 40, "the kit has its pieces");

    // Ids carry the kit. prefix scenes reference, and mesh paths are RELATIVE:
    // an absolute path here is what made the old editor's maps unportable.
    const KitPiece* wall = catalog.find("kit.wall");
    require(wall != nullptr, "kit.wall resolves");
    require(wall->meshPath == "meshes/kit/Wall_01.obj", "wall mesh path");
    require(wall->meshPath.front() != '/', "mesh paths are relative");
    require(wall->material == "Game/Kit/Dungeon", "wall material");
    require(wall->socket == Socket::Wall, "wall sits on an edge");
    require(nearly(wall->sizeKit.x, 20.0f) && nearly(wall->sizeKit.y, 20.0f) &&
                nearly(wall->sizeKit.z, 5.0f),
            "wall is 20x20x5 kit units");
    require(nearly(wall->sizeMeters(catalog.scale()).x, 4.0f),
            "a wall is one cell wide in metres");

    const KitPiece* floor = catalog.find("kit.floor");
    require(floor != nullptr && floor->socket == Socket::Floor, "kit.floor");
    require(catalog.find("kit.does_not_exist") == nullptr,
            "an unknown prefab resolves to null, not to a default");

    // span > 1 is what makes "derive the cell from the transform" unsafe, so it
    // must survive the parse.
    const KitPiece* hexagon = catalog.find("kit.floor_hexagon");
    require(hexagon != nullptr && hexagon->span == 2, "floor_hexagon spans 2");

    // Pieces authored around their mount rather than their base.
    const KitPiece* chandelier = catalog.find("kit.chandelier");
    require(chandelier != nullptr, "kit.chandelier resolves");
    require(chandelier->yOffsetKit < 0.0f, "a chandelier hangs below its origin");
    glm::vec3 min, max;
    chandelier->localBoundsMeters(catalog.scale(), min, max);
    require(min.y < 0.0f, "its bounds start below the origin");

    // Base-at-zero is the convention every non-exception piece follows.
    wall->localBoundsMeters(catalog.scale(), min, max);
    require(nearly(min.y, 0.0f) && nearly(max.y, 4.0f),
            "a wall stands from the floor to one cell up");
    require(nearly(min.x, -2.0f) && nearly(max.x, 2.0f),
            "and is centred on its origin in X");

    require(!catalog.bySocket(Socket::Wall).empty(), "sockets are queryable");
    require(!catalog.byRole("floor").empty(), "roles are queryable");
    require(catalog.roles().size() > 3, "roles are enumerable for the palette");

    const KitPiece* mannequin =
        catalog.find("kit.prop_humanoid_mannequin");
    require(mannequin && mannequin->meshPath ==
                              "meshes/props/prop_humanoid_mannequin.obj",
            "neutral humanoid test subject is placeable");
    const KitPiece* boss = catalog.find("kit.prop_boss_placeholder");
    require(boss && boss->meshPath ==
                          "meshes/props/prop_boss_placeholder.obj" &&
                boss->material == "Game/BossPlaceholder",
            "textured boss placeholder is placeable");
    require(boss && boss->attachments.size() == 1 &&
                boss->attachments.front().prefab ==
                    "kit.prop_boss_placeholder_sword" &&
                boss->attachments.front().position.x < 0.0f,
            "boss prefab owns its attached sword");
    const KitPiece* bossSword =
        catalog.find("kit.prop_boss_placeholder_sword");
    require(bossSword && bossSword->material == "Game/BossPlaceholderSword",
            "boss placeholder sword is independently placeable");
    // Counted by prefix rather than by role. `imported_model` is the role every
    // editor import writes, so a bare count of it asserted that nobody had
    // imported anything else -- a test that failed the moment an author used
    // the feature it was guarding. What it means is that *this* model kept all
    // of its parts.
    std::size_t modularParts = 0;
    for (const KitPiece* piece : catalog.byRole("imported_model"))
        if (piece->id.rfind("kit.import_modulardungeonfree", 0) == 0)
            ++modularParts;
    require(modularParts == 26,
            "GLB modular dungeon import exposes every mesh part");
    const KitPiece* fallback =
        catalog.find("kit.import_modulardungeonfree_p25");
    require(fallback &&
                fallback->material == "Engine/Psx/PrototypeSurface",
            "an untextured GLB part uses prototype surface fallback");

    KitCatalog missing;
    require(!KitCatalog::load("does/not/exist.toml", missing, error),
            "a missing catalogue fails");
    require(!error.empty(), "and says why");

    std::cout << "KitCatalogTests: ok (" << catalog.all().size() << " pieces)\n";
    return 0;
}
