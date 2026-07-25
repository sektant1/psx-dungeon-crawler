#pragma once
#include "FeelComponents.h"

#include <entt/entt.hpp>

namespace game::feel::poise {

// Chip `amount` off target poise. If it drops to <=0 (and the target is not in
// its post-stagger immunity window), break: refill poise to max and drive the
// target's ActionState into Staggered for kStaggerDuration. Returns true iff it
// broke this call. No-op if the target has no Poise. Pure (no physics/renderer).
bool apply(entt::registry& reg, entt::entity target, float amount);

// Advance every Poise: accumulate sinceHit, regen after regenDelay, and count
// down staggerImmuneFor.
void tick(entt::registry& reg, float dt);

} // namespace game::feel::poise
