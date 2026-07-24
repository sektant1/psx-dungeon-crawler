#pragma once
#include <entt/entt.hpp>

#include <vector>

namespace game::status {

// Fixed cadence for damage-over-time ticks (seconds). Burn magnitude is a
// per-second rate; it is dealt in discrete chunks at this interval.
inline constexpr float kDotInterval = 0.5f;

// Advance every entity's StatusEffects by dt: count down durations, deal Burn
// damage (mitigated by the target's Fire resistance), and drop expired effects.
// Entities dropped to 0 HP by a Burn tick are appended to `killed` so the caller
// can drive death visuals/events.
void tick(entt::registry& reg, float dt, std::vector<entt::entity>& killed);

// Aggregate movement-speed multiplier from Slow/Chill (product of 1-magnitude);
// 0 while Stunned or Rooted. 1 when unaffected.
float movementMultiplier(const entt::registry& reg, entt::entity e);
// May the entity perform actions (attacks)? False while Stunned.
bool canAct(const entt::registry& reg, entt::entity e);
// May the entity cast? False while Stunned or Silenced.
bool canCast(const entt::registry& reg, entt::entity e);

} // namespace game::status
