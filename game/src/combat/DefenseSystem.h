#pragma once
#include "FeelComponents.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace game::feel::defense {

// Poise damage a clean deflect deals to the attacker (big — deflect is how you
// win the poise war). Tunable.
inline constexpr float kDeflectPoisePunish = 45.0f;
// Default dodge cost / i-frame duration used by the live layer.
inline constexpr float kDodgeStamina = 25.0f;

// Enter the Deflecting state from Idle for kDeflectWindow seconds. Returns
// success (false if not idle).
bool beginDeflect(ActionState& as);

// Resolve an incoming hit against a defender. If the defender is currently in
// its Deflecting window, negate (return true) and chip the attacker's poise by
// kDeflectPoisePunish. Otherwise return false (caller lets damage through).
// `incomingPoise` is unused today but kept for future partial-deflect tuning.
bool resolveIncoming(entt::registry& reg, entt::entity defender,
                     entt::entity attacker, float incomingPoise);

// Start a dodge: pay kDodgeStamina, enter Dodging for `dur`, and grant `iframes`
// seconds of Health.invulnTimer (honored by damage::apply). Returns false if not
// idle or unaffordable. No-op on components that are absent.
bool beginDodge(entt::registry& reg, entt::entity e, float dur, float iframes);

// Kick a target: chip `poiseDmg` off its poise (can stagger via poise::apply)
// and return the world-space knockback impulse = normalize(dir)*force for the
// caller to apply to the target's physics body. Deals no HP damage.
glm::vec3 kick(entt::registry& reg, entt::entity target, glm::vec3 dir,
               float force, float poiseDmg);

} // namespace game::feel::defense
