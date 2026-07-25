#include "InteractionSystem.h"

#include "GameContext.h"
#include "LiveLevel.h"

#include <eng/Input.h>

namespace game {

void InteractionSystem::update(GameContext& ctx, LiveLevel& level, int depth,
                               glm::vec3 eye, glm::vec3 forward,
                               const TransitionFn& onDescend,
                               const TransitionFn& onAscend)
{
    eng::Input& in = ctx.input;

    mTargets.clear();
    level.appendTargets(mTargets, depth);
    const GameplayTarget* target = aimedTarget(mTargets, eye, forward);
    if (!target)
        return;

    if (target->kind == TargetKind::Torch) {
        if (in.wasPressed("interact"))
            level.toggleTorch(ctx.renderer, target->id);
    } else if (target->kind == TargetKind::PortalDown) {
        if (in.wasPressed("interact"))
            onDescend();
    } else {
        if (in.wasPressed("interact"))
            onAscend();
    }
}

} // namespace game
