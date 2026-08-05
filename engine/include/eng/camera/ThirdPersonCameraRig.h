#pragma once
#include <eng/Physics.h> // CollisionMask
#include <eng/camera/CameraRig.h>
#include <eng/ecs/components/ThirdPersonCamera.h>

namespace eng {

// An over-the-shoulder orbit camera with a spring arm and a lock-on.
//
// Node chain, and why it is four nodes rather than the two first person needs:
//
//   character   feet, turned by facingYaw -- what an avatar mesh parents to
//   pivot       smoothed focus + pivot height + shoulder offset
//     boom      the orbit: yaw and pitch, nothing else
//       eye     +Z along the boom, i.e. behind the look direction; the camera
//
// Keeping the boom's rotation and the eye's distance on separate nodes is what
// lets the spring arm write one number. The alternative -- computing a world
// eye position and pushing it onto one node -- recomputes the whole orbit every
// time a wall moves the camera a centimetre.
//
// The tuning IS eng::ecs::ThirdPersonCamera. There is no second struct and no
// translation step, so what an author edits in the inspector, what the .map
// stores and what the rig runs are the same fields (the mirror-not-translate
// rule the rest of the scene format already follows).
class ThirdPersonCameraRig final : public CameraRig {
public:
    void setTuning(const ecs::ThirdPersonCamera& tuning) { mTuning = tuning; }
    ecs::ThirdPersonCamera& tuning() { return mTuning; }
    const ecs::ThirdPersonCamera& tuning() const { return mTuning; }

    // Which layers the spring arm treats as walls. Default is every layer;
    // clearing a bit is how a game keeps the camera from being shoved by its
    // own trigger volumes and corpses.
    void setCollisionMask(CollisionMask mask) { mCollisionMask = mask; }
    void setPhysics(Physics* physics) override { mPhysics = physics; }

    void attach(Renderer& renderer) override;
    void detach(Renderer& renderer) override;
    void forgetNodes() override
    {
        mCharacter = {};
        mPivot = {};
        mBoomNode = {};
        mEye = {};
    }
    void present(Renderer& renderer, const CameraPose& pose, float dt) override;

    NodeHandle eyeNode() const override { return mEye; }
    NodeHandle characterNode() const override { return mCharacter; }
    glm::vec3 eyePosition() const override { return mEyeWorld; }
    glm::vec3 forward() const override;
    bool firstPerson() const override { return false; }
    glm::vec2 pitchLimitsRadians() const override;

    void setLockOn(const CameraLockOn& lock) override { mLock = lock; }
    bool viewOverride(float& yaw, float& pitch) const override;

    // What the rig decided this frame, before the walls get a say. Split out
    // so the whole framing model -- follow smoothing, pitch clamp, lock-on
    // blend -- is testable without a renderer, a physics world or a window.
    struct Solution {
        glm::vec3 pivot{0.0f}; // where the boom hangs from, world space
        glm::vec3 look{0.0f};  // the point the camera is framing
        float yaw = 0.0f;
        float pitch = 0.0f;
        float distance = 0.0f; // desired boom length, before collision
        bool locked = false;
    };
    // Advances the smoothing state by dt and returns this frame's framing.
    Solution solve(const CameraPose& pose, float dt);

    // The boom length after the walls: `blocked` is how far the spring arm may
    // extend, `desired` the length it wants. Shortens instantly -- a camera
    // that eases *into* a wall spends those frames inside it -- and lengthens
    // at pushOutSpeed, which is the asymmetry that stops a doorway strobing.
    // Pure, so that asymmetry is a test rather than a claim.
    float boomLength(float current, float desired, float blocked,
                     float dt) const;

    // Seeds the smoothing state so the first frame after a spawn or a level
    // change is already framed, instead of flying in from wherever the camera
    // was standing.
    void snapTo(const CameraPose& pose);

private:
    ecs::ThirdPersonCamera mTuning;
    CameraLockOn mLock;
    Physics* mPhysics = nullptr;
    CollisionMask mCollisionMask = kAllLayers;

    NodeHandle mCharacter{};
    NodeHandle mPivot{};
    NodeHandle mBoomNode{};
    NodeHandle mEye{};

    glm::vec3 mFocus{0.0f}; // smoothed feet position
    glm::vec3 mEyeWorld{0.0f};
    float mYaw = 0.0f;
    float mPitch = 0.0f;
    float mBoomLength = 0.0f; // current spring-arm length
    bool mSeeded = false;
    bool mLockedLastFrame = false;
};

} // namespace eng
