#include "Melee.h"
#include <algorithm>
#include <vector>

void MeleeSystem::startSwing() {
    if (swinging()) return;
    // Match ViewModel: 120 ms anticipation, then the 130 ms edge-leading cut.
    mWindupRemaining = 0.12f;
    mActiveRemaining = 0.13f;
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

    eng::BodyDesc sweep;
    sweep.kind   = eng::ShapeKind::Sphere;
    sweep.radius = mRadius;

    glm::vec3 sweepFrom = eye + forward * 0.3f;
    glm::vec3 sweepTo   = eye + forward * mReach;

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
        phys.applyImpulse(hit.body, forward * mImpulse, hit.point);
        if (mOnHit) mOnHit(hit.body, hit.point, hit.normal);
    }
}
