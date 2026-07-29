#include "DefenseSystem.h"
#include "PoiseSystem.h"
#include "StaminaSystem.h"
#include "CombatComponents.h" // Health (i-frames)

namespace game::feel::defense {

bool beginDeflect(ActionState& as) {
    if (as.phase != ActionPhase::Idle) return false;
    as.phase = ActionPhase::Deflecting;
    as.timer = kDeflectWindow;
    as.activeFiredThisStep = false;
    return true;
}

bool resolveIncoming(entt::registry& reg, entt::entity defender,
                     entt::entity attacker, float /*incomingPoise*/) {
    auto* as = reg.try_get<ActionState>(defender);
    if (!as || as->phase != ActionPhase::Deflecting || as->timer <= 0.0f) {
        return false;
    }
    poise::apply(reg, attacker, kDeflectPoisePunish);
    return true;
}

bool beginDodge(entt::registry& reg, entt::entity e, float dur, float iframes,
                float cost) {
    auto* as = reg.try_get<ActionState>(e);
    auto* st = reg.try_get<Stamina>(e);
    if (!as || !st) return false;
    if (as->phase != ActionPhase::Idle) return false;
    if (!stamina::spend(*st, cost)) return false;
    as->phase = ActionPhase::Dodging;
    as->timer = dur;
    if (auto* h = reg.try_get<Health>(e)) {
        h->invulnTimer = iframes;
    }
    return true;
}

glm::vec3 kick(entt::registry& reg, entt::entity target, glm::vec3 dir,
               float force, float poiseDmg) {
    poise::apply(reg, target, poiseDmg);
    float len = glm::length(dir);
    glm::vec3 n = len > 1e-4f ? dir / len : glm::vec3(0.0f);
    return n * force;
}

} // namespace game::feel::defense
