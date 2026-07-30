#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace game::showroom {

struct TreasureMotion {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    float lightPulse = 1.0f;
};

inline TreasureMotion treasureMotion(float animationTime,
                                     glm::vec3 origin = glm::vec3(0.0f))
{
    TreasureMotion motion;
    motion.position = origin + glm::vec3(
        0.0f, 1.35f + 0.25f * std::sin(animationTime * 0.9f), 0.0f);
    motion.orientation = glm::angleAxis(animationTime * 0.8f,
                                        glm::vec3(0, 1, 0));
    motion.lightPulse = 0.9f + 0.1f * std::sin(animationTime * 1.7f) +
                        0.05f * std::sin(animationTime * 4.3f);
    return motion;
}

} // namespace game::showroom
