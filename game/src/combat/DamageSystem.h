#pragma once
#include "CombatComponents.h"
#include "DamageTypes.h"

#include <entt/entt.hpp>

namespace game::damage {

// Mitigate a raw amount of one type against a resistance table. Pure; exposed
// for tests and for damage-preview UI. True damage is never mitigated.
float mitigate(float amount, DamageType type, const Resistances* resist);

// Resolve a hit against a target. The single choke point for all damage: applies
// friendly-fire and invulnerability gates, resistance mitigation, HP subtraction,
// and pushes the packet's crowd-control onto the target's StatusEffects. Stays
// physics/renderer-free -- the returned DamageResult carries the knockback and
// kill flag so the caller drives impulses/VFX/events.
DamageResult apply(entt::registry& reg, entt::entity target,
                   const DamagePacket& packet);

} // namespace game::damage
