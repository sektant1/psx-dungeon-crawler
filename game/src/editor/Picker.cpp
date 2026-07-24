#include "Picker.h"

#include <algorithm>
#include <cmath>

namespace editor {

Ray screenRay(glm::vec2 ndc, glm::vec3 camPos, glm::quat camOrient,
              float vFovRad, float aspect)
{
    const float tanHalf = std::tan(vFovRad * 0.5f);
    const glm::vec3 viewDir(ndc.x * tanHalf * aspect, ndc.y * tanHalf, -1.0f);
    Ray r;
    r.origin = camPos;
    r.dir = glm::normalize(camOrient * viewDir);
    return r;
}

bool rayAabb(const Ray& r, glm::vec3 mn, glm::vec3 mx, float& tHit)
{
    float tmin = 0.0f;
    float tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        const float o = r.origin[i], d = r.dir[i];
        if (std::fabs(d) < 1e-8f) {
            if (o < mn[i] || o > mx[i]) return false;
        } else {
            float t1 = (mn[i] - o) / d;
            float t2 = (mx[i] - o) / d;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    tHit = tmin;
    return true;
}

} // namespace editor
