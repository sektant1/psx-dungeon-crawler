#include "PoiseSystem.h"

#include <algorithm>

namespace game::feel::poise {

bool apply(entt::registry& reg, entt::entity target, float amount) {
    auto* p = reg.try_get<Poise>(target);
    if (!p) return false;
    if (p->staggerImmuneFor > 0.0f) return false; // grace: cannot be chipped

    p->sinceHit = 0.0f;
    p->current -= amount;
    if (p->current > 0.0f) return false;

    // Break: refill so the entity isn't perma-zero, then stagger.
    p->current = p->max;
    if (auto* as = reg.try_get<ActionState>(target)) {
        as->phase = ActionPhase::Staggered;
        as->timer = kStaggerDuration;
        as->activeFiredThisStep = false;
    }
    return true;
}

void tick(entt::registry& reg, float dt) {
    for (auto [e, p] : reg.view<Poise>().each()) {
        p.sinceHit += dt;
        if (p.staggerImmuneFor > 0.0f) {
            p.staggerImmuneFor = std::max(0.0f, p.staggerImmuneFor - dt);
        }
        if (p.sinceHit >= p.regenDelay && p.current < p.max) {
            p.current = std::min(p.max, p.current + p.regenRate * dt);
        }
    }
}

} // namespace game::feel::poise
