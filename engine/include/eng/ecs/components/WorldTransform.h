#pragma once
#include <glm/glm.hpp>

namespace eng::ecs {

// Composed world matrix, recomputed by World::updateWorldTransforms() for every
// entity tagged Dirty. Derived state: writing it is meaningless, because the
// next resolve overwrites it from the Transform chain.
struct WorldTransform {
    glm::mat4 matrix{1.0f};
};

} // namespace eng::ecs
