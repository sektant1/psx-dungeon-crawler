#pragma once
#include <eng/Handles.h>
#include <eng/Physics.h>
#include <glm/glm.hpp>
#include <functional>
#include <vector>

struct CombatConfig;

class MeleeSystem {
public:
    // called when a swing lands on a body (point/normal for VFX; impulse already applied)
    using HitFn = std::function<void(eng::BodyHandle, glm::vec3 point, glm::vec3 normal)>;
    void setHitCallback(HitFn fn) { mOnHit = std::move(fn); }

    // Live tunables (reach/radius/impulse/windup/active) read from CombatConfig.
    void setConfig(const CombatConfig* cfg) { mCfg = cfg; }

    void startSwing();                                  // begin an attack window
    bool swinging() const { return mWindupRemaining > 0.0f ||
                                   mActiveRemaining > 0.0f; }
    void fixedUpdate(eng::Physics&, glm::vec3 eye, glm::vec3 forward, float dt);

private:
    float mWindupRemaining = 0.0f;
    float mActiveRemaining = 0.0f;
    float mReach = 1.8f, mRadius = 0.5f, mImpulse = 6.0f;
    const CombatConfig* mCfg = nullptr;
    std::vector<uint32_t> mHitThisSwing;  // body ids already hit this swing
    HitFn mOnHit;
};
