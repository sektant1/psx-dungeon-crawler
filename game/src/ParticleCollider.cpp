#include "ParticleCollider.h"

#include <glm/geometric.hpp>

#include <cmath>

namespace game {

bool JoltParticleCollider::sweep(glm::vec3 from, glm::vec3 to,
                                 glm::vec3& hitPos, glm::vec3& hitNormal)
{
    const glm::vec3 delta = to - from;
    const float distance = glm::length(delta);
    // A particle that barely moved cannot have crossed a surface, and
    // normalising a zero-length segment would hand Jolt a NaN direction.
    if (!(distance > 1e-5f) || !std::isfinite(distance))
        return false;

    eng::RayHit hit;
    if (!mPhysics.rayCast(from, delta / distance, distance, hit, mMask))
        return false;

    hitPos = hit.point;
    hitNormal = hit.normal;
    return true;
}

} // namespace game
