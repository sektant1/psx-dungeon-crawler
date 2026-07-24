// mapgen <seed> <out.map> [assetDir]
// Generates a BSP dungeon and writes it as an editable .map.

#include "scene/LayoutToScene.h"
#include "scene/MapSerializer.h"
#include "DungeonGen.h"

#include <entt/entt.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::printf("usage: mapgen <seed> <out.map> [assetDir]\n");
        return 2;
    }
    const uint32_t seed = uint32_t(std::strtoul(argv[1], nullptr, 10));
    const std::string out = argv[2];
    const std::string assetDir = argc > 3 ? argv[3] : APP_ASSET_DIR;

    gen::Layout layout = gen::generate(seed);
    if (!layout.valid()) {
        std::printf("mapgen: generation failed: %s\n", layout.error().c_str());
        return 1;
    }

    entt::registry reg;
    game::SceneGenOptions opts;
    opts.tileDir = assetDir + "/meshes/tiles/";
    opts.propDir = assetDir + "/meshes/props/";
    game::layoutToScene(layout, opts, reg);

    if (!mapio::writeMap(out, reg, mapio::coreRegistry())) {
        std::printf("mapgen: failed to write %s\n", out.c_str());
        return 1;
    }
    std::printf("mapgen: wrote %s (seed %u)\n", out.c_str(), seed);
    return 0;
}
