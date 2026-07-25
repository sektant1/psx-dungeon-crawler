#pragma once
#include "DamageTypes.h"
#include "FeelComponents.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace game {

// Data definition of an attack's combat payload: what damage and crowd-control a
// landed hit produces. Delivery (melee arc / projectile / hitscan) is owned by
// CombatSystem; this is only the damage side, so one weapon can be reused across
// deliveries. Loaded from weapons.toml (WeaponLibrary) or built in code.
struct WeaponDef {
    std::string name = "unarmed";
    float baseDamage = 10.0f;
    DamageType damageType = DamageType::Physical;
    float critChance = 0.0f;      // 0..1
    float critMultiplier = 2.0f;  // damage x this on a crit
    float knockback = 0.0f;       // world impulse magnitude along the hit dir
    std::vector<CCApplication> ccOnHit; // effects applied to the victim on hit

    AttackDef timing{};        // windup/active/recovery/stamina/poise for this attack
    float drawTime = 0.0f;     // bows: seconds to full draw (0 = not a draw weapon)
    float fullDrawMult = 1.0f; // bows: damage multiplier at full draw

    // Build a resolved DamagePacket for a hit. `dir` is the normalized impact
    // direction (for knockback); `roll01` is a uniform [0,1) crit roll supplied
    // by the caller so the RNG source stays outside this pure builder.
    DamagePacket makePacket(entt::entity source, glm::vec3 dir,
                            float roll01) const {
        DamagePacket p;
        p.type = damageType;
        p.source = source;
        p.crit = roll01 < critChance;
        p.amount = baseDamage * (p.crit ? critMultiplier : 1.0f);
        p.knockback = dir * knockback;
        p.applies = ccOnHit;
        return p;
    }
};

} // namespace game
