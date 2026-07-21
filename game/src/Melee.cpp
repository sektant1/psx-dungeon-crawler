#include "Melee.h"
#include <vector>

void MeleeSystem::startSwing() {
    if (mActiveSteps > 0) return;
    mActiveSteps = 5;
    mHitThisSwing.clear();
}

void MeleeSystem::fixedUpdate(eng::Physics& phys, glm::vec3 eye, glm::vec3 forward, float /*dt*/) {
    if (mActiveSteps <= 0) return;
    --mActiveSteps;

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
