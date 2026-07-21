#pragma once

#include <glm/glm.hpp>

#include <vector>

enum class TargetKind { Torch, PortalDown, PortalUp };

struct GameplayTarget {
    TargetKind kind;
    int id = -1;
    glm::vec3 position{0.0f};
    float reach = 2.5f;
};

// Selects the nearest target inside its reach and the view cone. Torch and
// portal adapters feed the same seam, so arbitration policy lives once.
const GameplayTarget* aimedTarget(const std::vector<GameplayTarget>& targets,
                                  glm::vec3 eye, glm::vec3 forward,
                                  float minimumDot = 0.9f);
