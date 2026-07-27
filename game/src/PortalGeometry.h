#pragma once

#include <glm/glm.hpp>

#include <vector>

struct PortalGeometryDesc {
    float openingHalfWidth = 1.9f;
    float openingHalfHeight = 1.55f;
    float frameWidth = 0.34f;
    float frameDepth = 0.30f;
};

struct PortalBlock {
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
};

std::vector<PortalBlock>
buildSteppedPortalBlocks(const PortalGeometryDesc& desc);
