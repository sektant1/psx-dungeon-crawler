#include "StatusEffectSystem.h"

#include "CombatComponents.h"
#include "DamageSystem.h"

#include <algorithm>

namespace game::status {

void tick(entt::registry& reg, float dt, std::vector<entt::entity>& killed,
          BurnChannel burn)
{
    for (auto e : reg.view<StatusEffects>()) {
        auto& fx = reg.get<StatusEffects>(e);
        Health* hp = reg.try_get<Health>(e);
        const Resistances* resist = reg.try_get<Resistances>(e);

        for (ActiveEffect& fxa : fx.active) {
            if (fxa.kind == CrowdControl::Burn && hp && !hp->dead()) {
                fxa.tickAccum += dt;
                while (fxa.tickAccum >= kDotInterval) {
                    fxa.tickAccum -= kDotInterval;
                    const float raw = fxa.magnitude * kDotInterval;
                    hp->current -=
                        damage::mitigate(raw, burn.type, resist,
                                         burn.ignoresResistances);
                    if (hp->dead()) {
                        killed.push_back(e);
                        break;
                    }
                }
            }
            fxa.remaining -= dt;
        }

        // Drop expired effects.
        auto& v = fx.active;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [](const ActiveEffect& a) {
                                   return a.remaining <= 0.0f;
                               }),
                v.end());
    }
}

// Any active effect of the given kind?
static bool has(const entt::registry& reg, entt::entity e, CrowdControl kind)
{
    const StatusEffects* fx = reg.try_get<StatusEffects>(e);
    if (!fx)
        return false;
    for (const ActiveEffect& a : fx->active)
        if (a.kind == kind && a.remaining > 0.0f)
            return true;
    return false;
}

float movementMultiplier(const entt::registry& reg, entt::entity e)
{
    const StatusEffects* fx = reg.try_get<StatusEffects>(e);
    if (!fx)
        return 1.0f;
    float m = 1.0f;
    for (const ActiveEffect& a : fx->active) {
        if (a.remaining <= 0.0f)
            continue;
        if (a.kind == CrowdControl::Stun || a.kind == CrowdControl::Root)
            return 0.0f;
        if (a.kind == CrowdControl::Slow || a.kind == CrowdControl::Chill)
            m *= std::max(0.0f, 1.0f - a.magnitude);
    }
    return m;
}

bool canAct(const entt::registry& reg, entt::entity e)
{
    return !has(reg, e, CrowdControl::Stun);
}

bool canCast(const entt::registry& reg, entt::entity e)
{
    return !has(reg, e, CrowdControl::Stun) &&
           !has(reg, e, CrowdControl::Silence);
}

} // namespace game::status
