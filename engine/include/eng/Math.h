#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace eng {

// a..i are the ROWS of a pure-rotation basis, e.g. the Godot .tscn
// Transform3D value order. glm::mat3 takes columns, hence the transpose.
inline glm::quat quatFromBasisRows(float a, float b, float c, float d, float e,
                                   float f, float g, float h, float i)
{
    return glm::quat_cast(glm::mat3(a, d, g, b, e, h, c, f, i));
}

} // namespace eng
