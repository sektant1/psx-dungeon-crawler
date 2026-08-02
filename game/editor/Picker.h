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
                glm::vec2 viewportSize, glm::vec3 camPos, glm::quat camOrient,
                float vFovRad);

// World point -> screen pixel in the same viewport rect. False for points
// behind the camera.
bool projectToViewport(glm::vec3 world, const glm::mat4& viewProjection,
                       glm::vec2 viewportOrigin, glm::vec2 viewportSize,
                       glm::vec2& screenPoint);

bool rayAabb(const Ray& r, glm::vec3 mn, glm::vec3 mx, float& tHit);

// The same test, plus the outward normal of the face the ray entered through.
//
// Placement needs the face, not just the distance: pointing at the top of a
// crate and pointing at its side are different intentions, and the only thing
// that tells them apart is which slab won tmin. A ray whose origin is already
// inside the box has no entry face at all, and reports +Y -- the caller is
// standing in the geometry, and "up" is the only answer that keeps a ghost
// somewhere sane.
bool rayAabb(const Ray& r, glm::vec3 mn, glm::vec3 mx, float& tHit,
             glm::vec3& normal);

// Where a ray meets a horizontal plane at height `level`. False when the ray is
// parallel to it or points away -- the placement tool needs the difference.
bool rayPlaneY(const Ray& r, float level, glm::vec3& hit);

// The work-plane point under the cursor, always. When the ray misses the plane
// -- which is most of the time once the camera looks anywhere near the horizon
// -- it falls back to a point `fallbackDistance` along the ray, flattened onto
// the plane. The placement ghost has to be *somewhere*: vanishing whenever the
// view tips up is what makes the Place tool feel broken.
glm::vec3 workPlanePoint(const Ray& r, float level, float fallbackDistance);

} // namespace ed
