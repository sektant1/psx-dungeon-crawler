#include "PortalGeometry.h"

#include <cmath>

std::vector<PortalBlock>
buildSteppedPortalBlocks(const PortalGeometryDesc& desc)
{
    if (!std::isfinite(desc.openingHalfWidth) ||
        !std::isfinite(desc.openingHalfHeight) ||
        !std::isfinite(desc.frameWidth) ||
        !std::isfinite(desc.frameDepth) ||
        desc.openingHalfWidth <= 0.0f ||
        desc.openingHalfHeight <= 0.0f ||
        desc.frameWidth <= 0.0f ||
        desc.frameDepth <= 0.0f)
        return {};

    const float x = desc.openingHalfWidth;
    const float y = desc.openingHalfHeight;
    const float w = desc.frameWidth;
    const float d = desc.frameDepth;
    return {
        {{-x - w * 0.5f, 0.0f, 0.0f}, {w, y * 2.0f, d}},
        {{ x + w * 0.5f, 0.0f, 0.0f}, {w, y * 2.0f, d}},
        {{-x * 0.68f, y + w * 0.45f, 0.0f}, {x * 0.64f, w, d}},
        {{ x * 0.68f, y + w * 0.45f, 0.0f}, {x * 0.64f, w, d}},
        {{-x * 0.25f, y + w * 1.25f, 0.0f}, {x * 0.50f, w, d}},
        {{ x * 0.25f, y + w * 1.25f, 0.0f}, {x * 0.50f, w, d}},
        {{0.0f, y + w * 2.05f, 0.0f}, {x * 0.45f, w, d}},
    };
}
