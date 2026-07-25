#pragma once
#include "FeelComponents.h"

#include <entt/entt.hpp>

namespace game::feel::stamina {

// Try to pay `cost`. Returns false (and changes nothing) if unaffordable; on
// success subtracts and marks the spend so regen pauses. Pure.
bool spend(Stamina& s, float cost);

// Advance regen for every Stamina in the registry: accumulate sinceSpend, and
// once past regenDelay add regenRate*dt clamped to max.
void tick(entt::registry& reg, float dt);

} // namespace game::feel::stamina
