#include "Melee.h"
#include "CombatConfig.h"
#include <algorithm>
#include <vector>

void MeleeSystem::startSwing() {
    if (swinging()) return;
    // Match ViewModel: anticipation then the edge-leading cut (config-tunable).
    mWindupRemaining = mCfg ? mCfg->melee.windup : 0.12f;
    mActiveRemaining = mCfg ? mCfg->melee.active : 0.13f;
    mHitThisSwing.clear();
}

void MeleeSystem::fixedUpdate(eng::Physics& phys, glm::vec3 eye,
                              glm::vec3 forward, float dt) {
    if (mWindupRemaining > 0.0f) {
        mWindupRemaining = std::max(0.0f, mWindupRemaining - dt);
        return;
    }
    if (mActiveRemaining <= 0.0f) return;
    mActiveRemaining = std::max(0.0f, mActiveRemaining - dt);

    const float reach   = mCfg ? mCfg->melee.reach   : mReach;
    const float radius  = mCfg ? mCfg->melee.radius  : mRadius;
    const float impulse = mCfg ? mCfg->melee.impulse : mImpulse;

    eng::BodyDesc sweep;
    sweep.kind   = eng::ShapeKind::Sphere;
    sweep.radius = radius;

    glm::vec3 sweepFrom = eye + forward * 0.3f;
    glm::vec3 sweepTo   = eye + forward * reach;

    std::vector<eng::ShapeHit> hits;
    phys.shapeCast(sweep, sweepFrom, sweepTo, hits, eng::BodyLayer::Prop);

    for (const eng::ShapeHit& hit : hits) {
        uint32_t id = hit.body.id;
        bool alreadyHit = false;
        for (uint32_t seen : mHitThisSwing) {
            if (seen == id) { alreadyHit = true; break; }
        }
        if (alreadyHit) continue;

        mHitThisSwing.push_back(id);
        phys.applyImpulse(hit.body, forward * impulse, hit.point);
        if (mOnHit) mOnHit(hit.body, hit.point, hit.normal);
    }
}
