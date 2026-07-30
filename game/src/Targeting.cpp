#include "Targeting.h"

#include <glm/geometric.hpp>

// Aim is tested as a ray against each target's bounding sphere, not as a fixed
// view cone. A cone subtends a shrinking world-space radius as it closes, so a
// single dot threshold is wrong at both ends: the player had to stand well back
// to aim at a 2.25 m portal, and could tag a torch from far off-axis at range.
const GameplayTarget* aimedTarget(const std::vector<GameplayTarget>& targets,
                                  glm::vec3 eye, glm::vec3 forward)
{
    const GameplayTarget* best = nullptr;
    float bestDistance = 0.0f;
    for (const GameplayTarget& target : targets) {
        const glm::vec3 offset = target.position - eye;
        const float distance = glm::length(offset);
        if (distance < 1e-3f || distance > target.reach)
            continue;
        // Distance along the aim ray to the closest approach. Negative means
        // the target is behind the eye; inside the sphere it is small but
        // positive, which is exactly the point-blank case that must pass.
        const float along = glm::dot(offset, forward);
        if (along < 0.0f)
            continue;
        const float perpendicular = glm::length(offset - forward * along);
        if (perpendicular > target.radius)
            continue;
        if (!best || distance < bestDistance) {
            best = &target;
            bestDistance = distance;
        }
    }
    return best;
}
