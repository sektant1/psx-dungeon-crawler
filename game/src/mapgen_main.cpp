// mapgen <seed> <out.map>
// Generates a BSP dungeon and writes a cooked runtime .map.

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
        std::printf("usage: mapgen <seed> <out.map>\n");
        return 2;
    }
    const uint32_t seed = uint32_t(std::strtoul(argv[1], nullptr, 10));
    const std::string out = argv[2];

    gen::Layout layout = gen::generate(seed);
    if (!layout.valid()) {
        std::printf("mapgen: generation failed: %s\n", layout.error().c_str());
        return 1;
    }

    entt::registry reg;
    game::SceneGenOptions opts;
    if (!game::layoutToScene(layout, opts, reg)) {
        std::printf("mapgen: layout-to-scene conversion failed\n");
        return 1;
    }

    if (!mapio::writeMap(out, reg, mapio::coreRegistry())) {
        std::printf("mapgen: failed to write %s\n", out.c_str());
        return 1;
    }
    std::printf("mapgen: wrote %s (seed %u)\n", out.c_str(), seed);
    return 0;
}
