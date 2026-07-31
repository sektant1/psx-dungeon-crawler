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
    // Targets owned by systems outside the level (combatants, spawned props)
    // arbitrate against level targets instead of running a second aim test.
    mTargets.insert(mTargets.end(), mExtraTargets.begin(), mExtraTargets.end());
    mExtraTargets.clear();
    const GameplayTarget* target = aimedTarget(mTargets, eye, forward);
    if (!target)
        return;

    mFocus.available = true;
    mFocus.kind = target->kind;
    mFocus.id = target->id;
    mFocus.distance = glm::length(target->position - eye);
    mFocus.catalogIndex = target->catalogIndex;

    if (target->kind == TargetKind::Prop || target->kind == TargetKind::Actor) {
        // Look targets with no transition of their own: they exist so the HUD
        // can describe them. Interaction verbs for props land here when the
        // container systems arrive.
        return;
    }

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
