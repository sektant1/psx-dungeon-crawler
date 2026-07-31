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

// Screen pixel inside a docked viewport -> world ray. Keeping viewport offset,
// Y inversion and aspect here prevents every editor tool from growing a subtly
// different picker.
Ray viewportRay(glm::vec2 screenPoint, glm::vec2 viewportOrigin,
                glm::vec2 viewportSize, glm::vec3 camPos,
                glm::quat camOrient, float vFovRad);

// World point -> screen pixel in the same viewport rect. False for points
// behind the camera.
bool projectToViewport(glm::vec3 world, const glm::mat4& viewProjection,
                       glm::vec2 viewportOrigin, glm::vec2 viewportSize,
                       glm::vec2& screenPoint);

bool rayAabb(const Ray& r, glm::vec3 mn, glm::vec3 mx, float& tHit);

// Where a ray meets a horizontal plane at height `level`. False when the ray is
// parallel to it or points away -- the placement tool needs the difference.
bool rayPlaneY(const Ray& r, float level, glm::vec3& hit);

} // namespace ed
