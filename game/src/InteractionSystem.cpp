#include "InteractionSystem.h"

#include "GameContext.h"
#include "LiveLevel.h"

#include <eng/DebugUi.h>
#include <eng/Input.h>

namespace game {

void InteractionSystem::update(GameContext& ctx, LiveLevel& level, int depth,
                               glm::vec3 eye, glm::vec3 forward,
                               eng::DebugUi& ui, const TransitionFn& onDescend,
                               const TransitionFn& onAscend)
{
    eng::Input& in = ctx.input;

    mTargets.clear();
    level.appendTargets(mTargets, depth);
    const GameplayTarget* target = aimedTarget(mTargets, eye, forward);
    if (!target) {
        ui.setHudPrompt({});
    } else if (target->kind == TargetKind::Torch) {
        ui.setHudPrompt(level.torchIsLit(target->id)
                            ? "Press [E] to snuff the torch"
                            : "Press [E] to light the torch");
        if (in.wasPressed("interact"))
            level.toggleTorch(ctx.renderer, target->id);
    } else if (target->kind == TargetKind::PortalDown) {
        ui.setHudPrompt("Press [E] to descend");
        if (in.wasPressed("interact"))
            onDescend();
    } else {
        ui.setHudPrompt("Press [E] to ascend");
        if (in.wasPressed("interact"))
            onAscend();
    }
}

} // namespace game
