#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace editor {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 dir{0.0f, 0.0f, -1.0f};
};

Ray screenRay(glm::vec2 ndc, glm::vec3 camPos, glm::quat camOrient,
              float vFovRad, float aspect);

bool rayAabb(const Ray& r, glm::vec3 mn, glm::vec3 mx, float& tHit);

} // namespace editor
