#include "ActionStateSystem.h"
#include "StaminaSystem.h"

namespace game::feel::actionstate {

bool beginAttack(ActionState& as, Stamina& st, const AttackDef& def) {
    if (as.phase != ActionPhase::Idle) return false;
    if (!stamina::spend(st, def.staminaCost)) return false;
    as.attack = def;
    as.phase = ActionPhase::Windup;
    as.timer = def.windup;
    as.activeFiredThisStep = false;
    return true;
}

void advance(entt::registry& reg, float dt) {
    for (auto [e, as] : reg.view<ActionState>().each()) {
        as.activeFiredThisStep = false;
        if (as.phase == ActionPhase::Idle) continue;

        as.timer -= dt;
        if (as.timer > 0.0f) continue;

        switch (as.phase) {
            case ActionPhase::Windup:
                as.phase = ActionPhase::Active;
                as.timer = as.attack.active;
                as.activeFiredThisStep = true;
                break;
            case ActionPhase::Active:
                as.phase = ActionPhase::Recovery;
                as.timer = as.attack.recovery;
                break;
            case ActionPhase::Staggered:
                as.phase = ActionPhase::Idle;
                as.timer = 0.0f;
                if (auto* po = reg.try_get<Poise>(e)) {
                    po->staggerImmuneFor = kPostStaggerImmunity;
                }
                break;
            case ActionPhase::Recovery:
            case ActionPhase::Deflecting:
            case ActionPhase::Dodging:
            default:
                as.phase = ActionPhase::Idle;
                as.timer = 0.0f;
                break;
        }
    }
}

} // namespace game::feel::actionstate
