#pragma once
#include <glm/glm.hpp>

namespace eng {

struct LightDesc {
    enum class Type { Directional, Point };
    Type type = Type::Point;
    glm::vec3 colour{1.0f}; // linear, energy pre-multiplied by the caller
    float range = 3.0f;     // point lights only
    bool castShadows = false; // stencil shadows from opted-in casters
};

} // namespace eng
