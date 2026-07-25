#pragma once
#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <vector>

namespace game {

// Elemental damage channels. Resistances are keyed by this; True bypasses all
// mitigation. Keep Count last.
enum class DamageType {
    Physical,   // generic / environmental / unarmed fallback
    Slash,      // blades: sword
    Pierce,     // points: arrow, spear
    Blunt,      // impact: mace, hammer, kick
    Fire,
    Frost,
    Lightning,
    Poison,
    Arcane,
    True,
    Count
};
inline constexpr int kDamageTypeCount = static_cast<int>(DamageType::Count);

// Crowd-control / status kinds. Stun/Root/Silence are boolean gates (magnitude
// ignored); Slow/Chill carry a 0..1 movement-speed reduction; Burn carries a
// damage-per-second and ticks through the damage pipeline.
enum class CrowdControl {
    Stun,     // no move, no actions
    Root,     // no move, actions allowed
    Silence,  // no actions (casting), move allowed
    Slow,     // movement * (1 - magnitude)
    Chill,    // movement * (1 - magnitude); typically paired with Frost
    Burn,     // magnitude = damage per second, dealt as Fire over duration
    Count
};

// One crowd-control effect a hit wants to apply to its target.
struct CCApplication {
    CrowdControl kind = CrowdControl::Stun;
    float magnitude = 0.0f; // meaning depends on kind (see CrowdControl)
    float duration = 0.0f;  // seconds
};

// A single resolved hit. `amount` is post-crit but pre-mitigation; the resolver
// applies resistances. `source` is the attacker (for threat/attribution, and to
// skip self/friendly damage). `knockback` is a world-space impulse the caller
// applies to the target's physics body (the resolver stays physics-free).
struct DamagePacket {
    float amount = 0.0f;
    DamageType type = DamageType::Physical;
    entt::entity source = entt::null;
    bool crit = false; // informational, for floating-text / VFX
    glm::vec3 knockback{0.0f};
    std::vector<CCApplication> applies;
};

// What the resolver did, so the caller can drive VFX/audio/physics without
// re-reading component state.
struct DamageResult {
    bool hitLanded = false;   // false if blocked (invuln, friendly, dead, no Health)
    float dealt = 0.0f;       // post-mitigation damage actually subtracted
    bool crit = false;
    bool killed = false;      // this hit dropped the target to 0
    glm::vec3 knockback{0.0f}; // pass-through, applied only when hitLanded
};

} // namespace game
