#include "Targeting.h"

#include <cstdlib>

int main()
{
    std::vector<GameplayTarget> targets{
        {TargetKind::PortalDown, 0, {0.0f, 0.0f, -2.0f}, 3.0f},
        {TargetKind::Torch, 4, {0.0f, 0.0f, -1.0f}, 2.5f},
        {TargetKind::PortalUp, 0, {2.0f, 0.0f, 0.0f}, 3.0f},
    };
    const GameplayTarget* hit =
        aimedTarget(targets, glm::vec3(0.0f), {0.0f, 0.0f, -1.0f});
    if (!hit || hit->kind != TargetKind::Torch || hit->id != 4)
        return EXIT_FAILURE;
    if (aimedTarget(targets, glm::vec3(0.0f), {0.0f, 0.0f, 1.0f}))
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
