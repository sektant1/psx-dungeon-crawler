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

    // A switch with no default, deliberately. This was an if/else chain whose
    // final `else` meant "ascend", so every target kind added after it -- an
    // item on the floor, a person to talk to -- inherited the up-portal's verb:
    // aiming at a dropped potion and pressing interact took the player up a
    // level and cleared the focus, which is why nothing could be picked up. A
    // kind added now fails to compile here instead.
    switch (target->kind) {
    // Resolved and stopped, the same as Item and Npc above it: this system owns
    // no hideout and cannot pay for a tier. The app does, so the verb lives
    // there and this only decides what the crosshair is on.
    case TargetKind::Station:
        break;

    case TargetKind::Torch:
        mFocus.active = level.torchIsLit(target->id);
        if (in.wasPressed("interact"))
            level.toggleTorch(ctx.renderer, target->id);
        mFocus.active = level.torchIsLit(target->id);
        break;

    case TargetKind::PortalDown:
        if (in.wasPressed("interact")) {
            mFocus = {};
            onDescend();
        }
        break;

    case TargetKind::PortalUp:
        if (in.wasPressed("interact")) {
            mFocus = {};
            onAscend();
        }
        break;

    case TargetKind::Prop:
    case TargetKind::Actor:
    case TargetKind::Item:
    case TargetKind::Npc:
        // Look targets with no transition of their own. The focus is published
        // and that is all: taking an item and speaking to somebody are verbs
        // the app owns, because this system owns neither an inventory nor a
        // conversation.
        break;
    }
}

} // namespace game
