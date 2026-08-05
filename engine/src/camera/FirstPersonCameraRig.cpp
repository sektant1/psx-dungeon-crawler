#include <eng/camera/FirstPersonCameraRig.h>

#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace eng {

void FirstPersonCameraRig::attach(Renderer& r)
{
    mBody = r.createNode(kRootNode);
    mHead = r.createNode(mBody, {0.0f, 1.7f, 0.0f});
    r.attachCamera(mHead);
    mStepOffset = 0.0f;
    mLandDip = 0.0f;
    mLeanRadians = 0.0f;
    mHasLastFocus = false;
}

void FirstPersonCameraRig::detach(Renderer& r)
{
    // Destroying the body takes the head with it: the renderer's scene graph
    // owns children, and destroying the head first would leave the camera
    // parented to a node that is about to go.
    if (mBody.valid())
        r.destroyNode(mBody);
    mBody = {};
    mHead = {};
}

glm::vec2 FirstPersonCameraRig::pitchLimitsRadians() const
{
    const float limit = glm::radians(mTuning.pitchLimitDegrees);
    return {-limit, limit};
}

glm::vec3 FirstPersonCameraRig::forward() const
{
    const float cp = std::cos(mPitch);
    return {-std::sin(mYaw) * cp, std::sin(mPitch), -std::cos(mYaw) * cp};
}

void FirstPersonCameraRig::updateFeel(const CameraPose& pose, float dt)
{
    // --- stair smoothing -------------------------------------------------
    // The character controller steps up instantly, which is what makes stairs
    // walkable and what makes them look like teleports. The eye keeps its old
    // height and catches up; only while the character stays on the ground,
    // because a jump and a fall are vertical movement the player *meant*.
    if (mHasLastFocus) {
        const float dy = pose.focus.y - mLastFocusY;
        if (pose.grounded && mWasGrounded)
            mStepOffset = std::clamp(mStepOffset + dy, -mTuning.maxStepSmooth,
                                     mTuning.maxStepSmooth);
    }
    mLastFocusY = pose.focus.y;
    mHasLastFocus = true;
    mWasGrounded = pose.grounded;
    mStepOffset = smoothTowards(mStepOffset, 0.0f, mTuning.stepSmoothRate, dt);

    // --- landing dip -----------------------------------------------------
    if (pose.landingImpact > 0.0f)
        mLandDip = std::min(mTuning.landingDipMax,
                            mLandDip + pose.landingImpact *
                                           mTuning.landingDipPerSpeed);
    mLandDip = smoothTowards(mLandDip, 0.0f, mTuning.landingRecovery, dt);

    // --- strafe lean -----------------------------------------------------
    // Roll away from the direction of travel, the way a runner leans into a
    // turn. Small: this is a hint that the body is moving sideways, not a
    // camera effect anyone should be able to name while playing.
    const float target = -pose.strafeRatio * glm::radians(mTuning.leanDegrees);
    mLeanRadians = smoothTowards(mLeanRadians, target, mTuning.leanRate, dt);
}

void FirstPersonCameraRig::present(Renderer& r, const CameraPose& pose,
                                   float dt)
{
    updateFeel(pose, dt);

    mYaw = pose.viewYaw;
    mPitch = pose.viewPitch;

    // The composed head offset: what the simulation asked for, minus what the
    // feel layers are currently borrowing. Each term is one line, and muting
    // one is setting its parameter to zero rather than editing this expression.
    const glm::vec3 head = pose.headOffset -
                           glm::vec3(0.0f, mStepOffset + mLandDip, 0.0f);

    r.setPosition(mBody, pose.focus);
    r.setOrientation(mBody, glm::angleAxis(pose.facingYaw, glm::vec3(0, 1, 0)));
    r.setPosition(mHead, head + mShake.offset);
    const glm::vec3 shake = glm::radians(mShake.rotationDegrees);
    r.setOrientation(mHead,
                     glm::angleAxis(mPitch + shake.x, glm::vec3(1, 0, 0)) *
                         glm::angleAxis(pose.rollRadians + mLeanRadians +
                                            shake.z,
                                        glm::vec3(0, 0, 1)));

    // Where the eye ended up, for anything that needs the presented position
    // rather than the simulated one (audio, the collider overlay's fade).
    const float c = std::cos(pose.facingYaw);
    const float s = std::sin(pose.facingYaw);
    mEye = pose.focus + glm::vec3(c * head.x + s * head.z, head.y,
                                  -s * head.x + c * head.z);
}

} // namespace eng
