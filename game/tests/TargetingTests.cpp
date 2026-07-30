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

    // Point blank against a big target: the eye is 1 m from a 2.25 m portal
    // membrane, 0.5 m above its centre, looking level. A view cone rejected
    // this; the ray is 0.5 m off centre and well inside the radius.
    const std::vector<GameplayTarget> portal{
        {TargetKind::PortalDown, 0, {0.0f, 1.125f, -1.0f}, 3.0f, 1.125f},
    };
    if (!aimedTarget(portal, {0.0f, 1.625f, 0.0f}, {0.0f, 0.0f, -1.0f}))
        return EXIT_FAILURE;
    // Same distance, but aimed past the edge of the membrane: no focus.
    if (aimedTarget(portal, {0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, -1.0f}))
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
