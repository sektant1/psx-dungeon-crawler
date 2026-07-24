#include "DamageSystem.h"

#include <algorithm>

namespace game::damage {

float mitigate(float amount, DamageType type, const Resistances* resist)
{
    if (type == DamageType::True || !resist)
        return std::max(0.0f, amount);
    const float r = std::clamp((*resist)[type], -1.0f, 0.9f);
    return std::max(0.0f, amount * (1.0f - r));
}

DamageResult apply(entt::registry& reg, entt::entity target,
                   const DamagePacket& packet)
{
    DamageResult out;
    if (!reg.valid(target))
        return out;
    Health* hp = reg.try_get<Health>(target);
    if (!hp || hp->dead() || hp->invulnTimer > 0.0f)
        return out; // no health, already dead, or in i-frames

    // Friendly-fire gate: same non-Neutral faction deals no damage.
    if (reg.valid(packet.source)) {
        const FactionTag* sf = reg.try_get<FactionTag>(packet.source);
        const FactionTag* tf = reg.try_get<FactionTag>(target);
        if (sf && tf && sf->value == tf->value &&
            sf->value != Faction::Neutral)
            return out;
    }

    const Resistances* resist = reg.try_get<Resistances>(target);
    const float dealt = mitigate(packet.amount, packet.type, resist);
    hp->current -= dealt;

    // Push crowd-control onto the target (one container, ticked elsewhere).
    if (!packet.applies.empty()) {
        auto& fx = reg.get_or_emplace<StatusEffects>(target);
        for (const CCApplication& cc : packet.applies)
            fx.active.push_back(ActiveEffect{cc.kind, cc.magnitude, cc.duration,
                                             0.0f, packet.source});
    }

    out.hitLanded = true;
    out.dealt = dealt;
    out.crit = packet.crit;
    out.killed = hp->dead();
    out.knockback = packet.knockback;
    return out;
}

} // namespace game::damage
