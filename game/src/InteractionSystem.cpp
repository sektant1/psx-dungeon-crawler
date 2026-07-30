#include "InteractionSystem.h"

#include "GameContext.h"
#include "LiveLevel.h"

#include <eng/Input.h>

#include <glm/geometric.hpp>

namespace game {

void InteractionSystem::update(GameContext& ctx, LiveLevel& level, int depth,
                               glm::vec3 eye, glm::vec3 forward,
                               const TransitionFn& onDescend,
                               const TransitionFn& onAscend)
{
    eng::Input& in = ctx.input;
    mFocus = {};

    mTargets.clear();
    level.appendTargets(mTargets, depth);
    const GameplayTarget* target = aimedTarget(mTargets, eye, forward);
    if (!target)
        return;

    mFocus.available = true;
    mFocus.kind = target->kind;
    mFocus.id = target->id;
    mFocus.distance = glm::length(target->position - eye);

    if (target->kind == TargetKind::Torch) {
        mFocus.active = level.torchIsLit(target->id);
        if (in.wasPressed("interact"))
            level.toggleTorch(ctx.renderer, target->id);
        mFocus.active = level.torchIsLit(target->id);
    } else if (target->kind == TargetKind::PortalDown) {
        if (in.wasPressed("interact")) {
            mFocus = {};
            onDescend();
        }
    } else {
        if (in.wasPressed("interact")) {
            mFocus = {};
            onAscend();
        }
    }
}

} // namespace game
