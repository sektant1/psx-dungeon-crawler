#pragma once

#include <glm/glm.hpp>

namespace editor {

struct Ray;

enum class GizmoMode { Translate, Rotate, Scale };

bool closestPointOnAxis(glm::vec3 axisOrigin, glm::vec3 axisDir,
                        const Ray& ray, float& t);

bool rayPlane(const Ray& ray, glm::vec3 planePoint, glm::vec3 normal,
              glm::vec3& hit);

float snap(float v, float step);

// Signed angle (radians) rotating `from` to `to` about `axis`, using the
// right-hand rule around `axis`. Vectors need not be unit or perpendicular to
// the axis; only their components in the axis plane matter for the sign.
float signedAngleAround(glm::vec3 from, glm::vec3 to, glm::vec3 axis);

} // namespace editor
