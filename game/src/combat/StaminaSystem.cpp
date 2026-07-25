#include "StaminaSystem.h"

#include <algorithm>

namespace game::feel::stamina {

bool spend(Stamina& s, float cost) {
    if (s.current < cost) return false;
    s.current -= cost;
    s.sinceSpend = 0.0f;
    return true;
}

void tick(entt::registry& reg, float dt) {
    for (auto [e, s] : reg.view<Stamina>().each()) {
        s.sinceSpend += dt;
        if (s.sinceSpend >= s.regenDelay && s.current < s.max) {
            s.current = std::min(s.max, s.current + s.regenRate * dt);
        }
    }
}

} // namespace game::feel::stamina
