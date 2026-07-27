#include "PortalGeometry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "PortalGeometryTests: " << message << '\n';
        std::exit(1);
    }
}
bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }
}

int main()
{
    PortalGeometryDesc desc;
    desc.openingHalfWidth = 1.9f;
    desc.openingHalfHeight = 1.55f;
    desc.frameWidth = 0.34f;
    desc.frameDepth = 0.30f;
    const auto blocks = buildSteppedPortalBlocks(desc);
    require(blocks.size() == 7, "stepped arch does not have seven blocks");
    for (const PortalBlock& block : blocks) {
        require(block.scale.x > 0 && block.scale.y > 0 &&
                    block.scale.z > 0,
                "portal block has invalid scale");
        require(near(block.scale.z, desc.frameDepth),
                "portal block depth changed");
    }
    require(near(blocks[0].position.x, -blocks[1].position.x),
            "portal pillars are not symmetric");
    require(near(blocks[2].position.x, -blocks[3].position.x),
            "portal shoulders are not symmetric");
    require(near(blocks[4].position.x, -blocks[5].position.x),
            "portal upper shoulders are not symmetric");
    require(near(blocks[6].position.x, 0.0f),
            "portal keystone is not centered");

    desc.frameWidth = 0.0f;
    require(buildSteppedPortalBlocks(desc).empty(),
            "invalid portal width was accepted");
    desc = {};
    desc.openingHalfHeight = std::numeric_limits<float>::infinity();
    require(buildSteppedPortalBlocks(desc).empty(),
            "non-finite portal height was accepted");

    std::cout << "PortalGeometryTests OK\n";
}
