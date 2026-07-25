#pragma once
#include "FeelComponents.h"

#include <entt/entt.hpp>

namespace game::feel::actionstate {

// Start an attack: only from Idle, and only if `def.staminaCost` is affordable.
// On success pays stamina, stores the def, and enters Windup. Returns success.
bool beginAttack(ActionState& as, Stamina& st, const AttackDef& def);

// Advance every ActionState by dt. Clears activeFiredThisStep for all first,
// then per phase:
//   Windup   -> on expiry: Active (sets activeFiredThisStep=true, timer=active)
//   Active   -> on expiry: Recovery (timer=recovery)
//   Recovery/Deflecting/Dodging -> on expiry: Idle
//   Staggered-> on expiry: Idle, and grant kPostStaggerImmunity to Poise if any
void advance(entt::registry& reg, float dt);

} // namespace game::feel::actionstate
