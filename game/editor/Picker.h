#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ed {

// Screen-space picking. Recovered from the editor deleted in 4cfff04 -- the
// maths did not rot, and its tests came back with it.
struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 dir{0.0f, 0.0f, -1.0f};
};

Ray screenRay(glm::vec2 ndc, glm::vec3 camPos, glm::quat camOrient,
              float vFovRad, float aspect);

bool rayAabb(const Ray& r, glm::vec3 mn, glm::vec3 mx, float& tHit);

// Where a ray meets a horizontal plane at height `level`. False when the ray is
// parallel to it or points away -- the placement tool needs the difference.
bool rayPlaneY(const Ray& r, float level, glm::vec3& hit);

} // namespace ed
