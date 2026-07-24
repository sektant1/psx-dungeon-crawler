#include "Gizmo.h"

#include "Picker.h"

#include <cmath>

namespace editor {

bool closestPointOnAxis(glm::vec3 axisOrigin, glm::vec3 axisDir,
                        const Ray& ray, float& t)
{
    const glm::vec3 u = axisDir;
    const glm::vec3 v = ray.dir;
    const glm::vec3 w0 = axisOrigin - ray.origin;
    const float a = glm::dot(u, u);
    const float b = glm::dot(u, v);
    const float c = glm::dot(v, v);
    const float d = glm::dot(u, w0);
    const float e = glm::dot(v, w0);
    const float denom = a * c - b * b;
    if (std::fabs(denom) < 1e-8f) return false;
    t = (b * e - c * d) / denom;
    return true;
}

bool rayPlane(const Ray& ray, glm::vec3 planePoint, glm::vec3 normal,
              glm::vec3& hit)
{
    const float denom = glm::dot(normal, ray.dir);
    if (std::fabs(denom) < 1e-8f) return false;
    const float t = glm::dot(normal, planePoint - ray.origin) / denom;
    if (t < 0.0f) return false;
    hit = ray.origin + t * ray.dir;
    return true;
}

float snap(float v, float step)
{
    if (step <= 0.0f) return v;
    return std::round(v / step) * step;
}

float signedAngleAround(glm::vec3 from, glm::vec3 to, glm::vec3 axis)
{
    const glm::vec3 n = glm::normalize(axis);
    // Project both vectors onto the plane perpendicular to the axis.
    const glm::vec3 f = from - n * glm::dot(from, n);
    const glm::vec3 t = to - n * glm::dot(to, n);
    if (glm::dot(f, f) < 1e-12f || glm::dot(t, t) < 1e-12f) return 0.0f;
    return std::atan2(glm::dot(glm::cross(f, t), n), glm::dot(f, t));
}

} // namespace editor
