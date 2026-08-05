#pragma once
#include <eng/camera/CameraRig.h>

namespace eng {

// The view from inside the character's head: a yaw-only body node with a
// pitched head node under it, which is the chain every first-person viewmodel
// and carried light in this project already hangs off.
//
// It is deliberately the *same* chain FpsController used to build itself, so
// moving it here changed no render node, no attachment point and no image. What
// it adds is the feel layer that had nowhere to live: stair smoothing, a
// landing dip and a strafe lean, each a named parameter rather than another
// term in one expression.
class FirstPersonCameraRig final : public CameraRig {
public:
    struct Tuning {
        // Stair smoothing. The character controller steps up instantly -- that
        // is what makes stairs walkable -- and the eye should not. The eye
        // keeps its height and catches up exponentially, which is the single
        // biggest readability win in a stepped dungeon.
        float stepSmoothRate = 14.0f; // per second
        float maxStepSmooth = 0.55f;  // metres of step the eye will absorb
        // Touchdown dip: metres of drop per m/s of impact, capped, then
        // recovered exponentially. Restrained on purpose -- the viewmodel has
        // the loud version of this, and a camera that dives on every landing
        // reads as a stumble.
        float landingDipPerSpeed = 0.010f;
        float landingDipMax = 0.075f;
        float landingRecovery = 9.0f;
        // Lean: degrees of roll at a full-speed strafe, eased in and out.
        float leanDegrees = 1.1f;
        float leanRate = 7.0f;
        // How far the neck bends. Not 90: at exactly vertical the yaw axis
        // degenerates and the horizon spins.
        float pitchLimitDegrees = 89.0f;
    };

    void setTuning(const Tuning& tuning) { mTuning = tuning; }
    Tuning& tuning() { return mTuning; }
    const Tuning& tuning() const { return mTuning; }

    void attach(Renderer& renderer) override;
    void detach(Renderer& renderer) override;
    void forgetNodes() override
    {
        mBody = {};
        mHead = {};
    }
    void present(Renderer& renderer, const CameraPose& pose, float dt) override;

    NodeHandle eyeNode() const override { return mHead; }
    NodeHandle characterNode() const override { return mBody; }
    glm::vec3 eyePosition() const override { return mEye; }
    glm::vec3 forward() const override;
    bool firstPerson() const override { return true; }
    glm::vec2 pitchLimitsRadians() const override;

private:
    // The feel layers, kept apart so each is one line to mute and one line to
    // read: the composed head offset is the sum of them, not a function that
    // knows all three.
    void updateFeel(const CameraPose& pose, float dt);

    Tuning mTuning;
    NodeHandle mBody{};
    NodeHandle mHead{};
    glm::vec3 mEye{0.0f};
    float mYaw = 0.0f;
    float mPitch = 0.0f;
    // How far below the body's own head height the eye currently sits while it
    // catches up with a step, and the height the body was at last frame.
    float mStepOffset = 0.0f;
    float mLastFocusY = 0.0f;
    float mLandDip = 0.0f;
    float mLeanRadians = 0.0f;
    bool mHasLastFocus = false;
    bool mWasGrounded = true;
};

} // namespace eng
