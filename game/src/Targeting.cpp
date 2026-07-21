#include "Targeting.h"

#include <glm/geometric.hpp>

const GameplayTarget* aimedTarget(const std::vector<GameplayTarget>& targets,
                                  glm::vec3 eye, glm::vec3 forward,
                                  float minimumDot)
{
    const GameplayTarget* best = nullptr;
    float bestDistance = 0.0f;
    for (const GameplayTarget& target : targets) {
        const glm::vec3 offset = target.position - eye;
        const float distance = glm::length(offset);
        if (distance < 1e-3f || distance > target.reach ||
            glm::dot(offset / distance, forward) < minimumDot)
            continue;
        if (!best || distance < bestDistance) {
            best = &target;
            bestDistance = distance;
        }
    }
    return best;
}
