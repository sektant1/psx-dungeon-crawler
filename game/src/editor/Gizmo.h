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

} // namespace editor
