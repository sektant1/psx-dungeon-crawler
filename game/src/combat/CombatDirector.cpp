#include "CombatDirector.h"

#include "DamageSystem.h"
#include "StatusEffectSystem.h"

#include <eng/Physics.h>

#include <algorithm>

namespace game {

void CombatDirector::init(const std::string& weaponsTomlPath,
                          const CombatVocabulary& vocabulary)
{
    mVocabulary = &vocabulary;
    mWeapons.load(weaponsTomlPath); // defaults remain if the file is absent
    mWeapons.resolve(vocabulary);
}

entt::entity CombatDirector::addCombatant(eng::BodyHandle body, const Health& hp,
                                          const Resistances& resist,
                                          Faction faction)
{
    const entt::entity e = mReg.create();
    mReg.emplace<Health>(e, hp);
    mReg.emplace<Resistances>(e, resist);
    mReg.emplace<FactionTag>(e, faction);
    mReg.emplace<BodyLink>(e, body);
    if (body.valid())
        mByBody[body.id] = e;
    return e;
}

void CombatDirector::removeCombatant(eng::BodyHandle body)
{
    auto it = mByBody.find(body.id);
    if (it == mByBody.end())
        return;
    if (mReg.valid(it->second))
        mReg.destroy(it->second);
    mByBody.erase(it);
}

entt::entity CombatDirector::entityForBody(eng::BodyHandle body) const
{
    auto it = mByBody.find(body.id);
    return it == mByBody.end() ? entt::null : it->second;
}

void CombatDirector::hitBody(eng::Physics& physics, eng::BodyHandle victimBody,
                             const std::string& weaponId, entt::entity source,
                             glm::vec3 dir, glm::vec3 atPoint)
{
    const entt::entity target = entityForBody(victimBody);
    if (target == entt::null)
        return;

    const WeaponDef& w = mWeapons.get(weaponId);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const DamagePacket packet = w.makePacket(source, dir, unit(mRng));

    const DamageResult res = damage::apply(mReg, target, packet);
    if (!res.hitLanded)
        return;
    if (glm::length(res.knockback) > 0.0f)
        physics.applyImpulse(victimBody, res.knockback, atPoint);
    if (res.killed && mOnDeath)
        mOnDeath(target);
}

void CombatDirector::tick(float dt)
{
    // Count down i-frames.
    for (auto e : mReg.view<Health>()) {
        Health& h = mReg.get<Health>(e);
        if (h.invulnTimer > 0.0f)
            h.invulnTimer = std::max(0.0f, h.invulnTimer - dt);
    }
    // Status effects (Burn DoT may kill).
    mKilledScratch.clear();
    status::BurnChannel burn;
    if (mVocabulary) {
        burn.type = mVocabulary->burnDamageType();
        burn.ignoresResistances = mVocabulary->bypassesMitigation(burn.type);
    }
    status::tick(mReg, dt, mKilledScratch, burn);
    if (mOnDeath)
        for (entt::entity e : mKilledScratch)
            mOnDeath(e);
}

} // namespace game
