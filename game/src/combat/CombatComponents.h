#pragma once
#include "DamageTypes.h"

#include <eng/Handles.h>

#include <vector>

namespace game {

// Links a combat entity to its physics body, so knockback resolves the body
// from the entity and contacts resolve the entity from a body hit.
struct BodyLink {
    eng::BodyHandle body;
};

// Hit points. invulnTimer > 0 blocks all incoming damage (i-frames after a hit
// or on spawn). Entities without Health cannot be damaged.
struct Health {
    float current = 100.0f;
    float max = 100.0f;
    float invulnTimer = 0.0f;
    bool dead() const { return current <= 0.0f; }
};

// Per-type damage mitigation. value[t] in [-1, 0.9]: 0 = neutral, positive =
// resist (0.5 halves that type), negative = vulnerable (-0.5 = +50% taken).
// True damage ignores this table entirely. Absent component = all-neutral.
struct Resistances {
    float value[kDamageTypeCount] = {0.0f};

    float& operator[](DamageType t) { return value[static_cast<int>(t)]; }
    float operator[](DamageType t) const { return value[static_cast<int>(t)]; }
};

// Team, for friendly-fire gating. Same non-Neutral faction does not damage.
enum class Faction { Player, Enemy, Neutral };
struct FactionTag {
    Faction value = Faction::Neutral;
};

// A live crowd-control/status instance on an entity. `remaining` counts down;
// `tickAccum` paces damage-over-time (Burn) at a fixed interval.
struct ActiveEffect {
    CrowdControl kind = CrowdControl::Stun;
    float magnitude = 0.0f;
    float remaining = 0.0f;
    float tickAccum = 0.0f;
    entt::entity source = entt::null;
};

// Container of active effects on an entity. One StatusEffectSystem ticks all of
// them; adding a new effect kind needs no new component.
struct StatusEffects {
    std::vector<ActiveEffect> active;
};

} // namespace game
