#include "Picker.h"

#include <algorithm>
#include <cmath>

namespace ed {

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

Ray viewportRay(glm::vec2 screenPoint, glm::vec2 viewportOrigin,
                glm::vec2 viewportSize, glm::vec3 camPos, glm::quat camOrient,
                float vFovRad)
{
    const glm::vec2 within = (screenPoint - viewportOrigin) / viewportSize;
    const glm::vec2 ndc{within.x * 2.0f - 1.0f, 1.0f - within.y * 2.0f};
    return screenRay(ndc, camPos, camOrient, vFovRad,
                     viewportSize.x / viewportSize.y);
}

bool projectToViewport(glm::vec3 world, const glm::mat4& viewProjection,
                       glm::vec2 viewportOrigin, glm::vec2 viewportSize,
                       glm::vec2& screenPoint)
{
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
    if (clip.w <= 1e-4f)
        return false;
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    screenPoint = viewportOrigin +
                  glm::vec2((ndc.x * 0.5f + 0.5f) * viewportSize.x,
                            (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y);
    return true;
}

bool rayAabb(const Ray& r, glm::vec3 mn, glm::vec3 mx, float& tHit)
{
    float tmin = 0.0f;
    float tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        const float o = r.origin[i], d = r.dir[i];
        if (std::fabs(d) < 1e-8f) {
            if (o < mn[i] || o > mx[i])
                return false;
        }
        else {
            float t1 = (mn[i] - o) / d;
            float t2 = (mx[i] - o) / d;
            if (t1 > t2)
                std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax)
                return false;
        }
    }
    tHit = tmin;
    return true;
}

bool rayPlaneY(const Ray& r, float level, glm::vec3& hit)
{
    if (std::fabs(r.dir.y) < 1e-6f)
        return false;
    const float t = (level - r.origin.y) / r.dir.y;
    if (t < 0.0f)
        return false;
    hit = r.origin + r.dir * t;
    return true;
}

glm::vec3 workPlanePoint(const Ray& r, float level, float fallbackDistance)
{
    glm::vec3 hit;
    if (rayPlaneY(r, level, hit))
        return hit;
    // Behind, above or parallel: take a point out along the ray and drop it to
    // the plane, so the ghost keeps tracking the cursor's direction instead of
    // disappearing.
    const glm::vec3 ahead = r.origin + r.dir * fallbackDistance;
    return glm::vec3(ahead.x, level, ahead.z);
}

} // namespace ed
