#pragma once
#include <eng/Handles.h>
#include <eng/Physics.h>
#include <glm/glm.hpp>
#include <functional>
#include <vector>

class MeleeSystem {
public:
    // called when a swing lands on a body (point/normal for VFX; impulse already applied)
    using HitFn = std::function<void(eng::BodyHandle, glm::vec3 point, glm::vec3 normal)>;
    void setHitCallback(HitFn fn) { mOnHit = std::move(fn); }

    void startSwing();                                  // begin an attack window
    bool swinging() const { return mActiveSteps > 0; }
    void fixedUpdate(eng::Physics&, glm::vec3 eye, glm::vec3 forward, float dt);

private:
    int mActiveSteps = 0;                 // fixed steps remaining in the swing
    float mReach = 1.8f, mRadius = 0.5f, mImpulse = 6.0f;
    std::vector<uint32_t> mHitThisSwing;  // body ids already hit this swing
    HitFn mOnHit;
};
