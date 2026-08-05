#pragma once
#include <eng/Handles.h>
#include <eng/Physics.h> // CollisionMask / kAllLayers used by value below

#include <glm/glm.hpp>
#include <limits>

namespace eng {
class Input;
class Physics;
class Renderer;

// Reusable grounded first-person controller:
// acceleration/deceleration, exhaustion-safe sprint stamina, grounded jump,
// sprint-slide, hold-to-crouch and restrained camera bob/FOV feedback. Yaw
// lives on the body, pitch on the head.
class FpsController
{
public:
    struct Command {
        glm::vec2 move{0.0f}; // x = right, y = forward
        glm::vec2 lookDelta{0.0f};
        bool mouseLook = false;
        bool sprint = false;
        bool walk = false;
        bool crouch = false;
        bool jumpPressed = false;
        bool slidePressed = false;
    };

    void init(eng::Renderer& r, eng::Physics& physics, glm::vec3 startPos,
              float speed, float sensitivity, glm::vec3 roomMin,
              glm::vec3 roomMax);
    void reset(glm::vec3 startPos, float speed, float sensitivity,
               glm::vec3 roomMin, glm::vec3 roomMax, float baseFov = 70.0f);
    // Locomotion, at the simulation's fixed rate. Everything that touches the
    // character controller lives here: running it on the render delta made the
    // player's motion frame-rate dependent and, because the world around it
    // steps at a fixed 60 Hz, visibly unstable at high frame rates -- worst
    // while sprinting, where the per-frame displacement is largest.
    void simulate(const Command& command, float fixedDt);

    // Mouse look, at the render rate. Deliberately not part of simulate:
    // quantising the view to the simulation rate reads as input lag in first
    // person, and there is no physics riding on the camera's orientation.
    void applyLook(const Command& command);

    // `alpha` is the fraction of the way from the previous fixed step to the
    // current one (Physics::interpolationAlpha). Position is interpolated
    // between them; orientation is already current.
    void present(eng::Renderer& r, float alpha = 1.0f);
    // Camera shake, in camera space, applied at presentation only.
    //
    // Deliberately NOT part of the simulated pose: eyePosition(), the physics
    // body and everything that aims read the unshaken transform, so a shake can
    // never desync collision or send a shot somewhere the player did not point.
    // The caller recomputes it every frame and never reads it back.
    void setViewShake(glm::vec3 offset, glm::vec3 rotationDegrees)
    {
        mShakeOffset = offset;
        mShakeRotationDegrees = rotationDegrees;
    }

    // Reads input, looks, simulates one step and presents. For callers with no
    // fixed-step loop of their own -- tests and tools. The game drives the
    // three phases separately.
    void update(eng::Input& in, eng::Renderer& r, float dt);

    // Fills a Command from the current input state.
    static Command readCommand(eng::Input& in);

    // Dodge/dash tuning, from data. Duration is short and speed high on
    // purpose: the dash is a commitment, not a movement option -- you go where
    // you pointed, at a fixed distance, and you cannot steer out of it.
    struct DashTuning {
        float speed = 14.0f;    // m/s during the dash
        float duration = 0.32f; // seconds
        float cooldown = 0.45f; // seconds before another dash is allowed
        float cameraDrop = 0.34f; // cosmetic crouch arc, metres
        // Whole turns only; setDashTuning rounds to nearest 360 degrees so a
        // completed dash always returns camera to identity. Zero disables.
        float cameraRollDegrees = 360.0f;
    };
    void setDashTuning(const DashTuning& t);
    const DashTuning& dashTuning() const { return mDash; }

    // Start a dash. `direction` is world-space XZ; a zero direction dashes
    // backwards, which is the souls-style backstep you get from a neutral
    // input. Returns false while another dash is running or cooling down, so
    // the caller knows not to spend stamina or grant i-frames.
    bool beginDash(glm::vec2 direction);
    bool dashing() const { return mDashTime > 0.0f; }
    float dashCameraDrop() const { return mDashCameraDrop; }
    float dashRollRadians() const { return mDashRoll; }
    // Movement direction the current input maps to, world-space XZ, normalised.
    // Zero when there is no movement input. Callers building a dash direction
    // want this rather than re-deriving it from yaw.
    glm::vec2 inputDirection(const Command& command) const;

    // Layers the character body sweeps against, narrowing the world collision
    // matrix for this character only. Default is every layer, i.e. the matrix
    // alone. Clear a bit to make that layer pass-through for the player --
    // phasing through props during a dash, a gate that only enemies collide
    // with -- without editing the matrix every other body shares.
    void setCollisionMask(CollisionMask m) { mCollisionMask = m; }
    CollisionMask collisionMask() const { return mCollisionMask; }

    float& speed() { return mSpeed; }
    float& sensitivity() { return mSens; }
    float sprintStamina() const { return mSprintStamina; } // normalized 0..1
    bool crouched() const { return mCrouched; }
    bool sprinting() const { return mSprinting; }
    bool sliding() const { return mSliding; }
    bool grounded() const { return mPhysics ? mCharGrounded : (mPos.y <= 0.001f); }
    glm::vec3 groundNormal() const { return mGroundNormal; }
    float horizontalSpeed() const { return glm::length(mVelocity); }
    // Metres per second up the world axis. Positive is rising, which is how a
    // caller distinguishes a jump from walking off a ledge -- both leave the
    // ground, and only one of them is something the player did.
    float verticalSpeed() const { return mVerticalVelocity; }
    glm::vec3 position() const { return mPos; }
    // Horizontal capsule footprint, useful to gameplay-side movement sweeps.
    float collisionRadius() const { return mCollisionRadius; }
    // Keeps locomotion feedback separate from a designer/debug-camera FOV.
    float baseFov() const { return mBaseFov; }
    void setBaseFov(float degrees);
    // Optional game-geometry constraint. Infinity (default) disables it.
    void setCeilingHeight(float height) { mCeilingHeight = height; }
    void setViewAngles(float yawRadians, float pitchRadians = 0.0f);

    // Camera-feel knobs (debug-UI tunable): sprint FOV kick in degrees, head-bob
    // vertical amplitude in metres, and head-bob cycle speed.
    float& sprintFovKick() { return mSprintFovKick; }
    float& bobAmount() { return mBobAmount; }
    float& bobSpeed() { return mBobSpeed; }

    // Eye position (feet + eye height) and view direction, for interaction
    // ray checks.
    glm::vec3 eyePosition() const;
    glm::vec3 forward() const;

    // Head node (camera parent), e.g. for attaching a player-carried light.
    NodeHandle headNode() const { return mHead; }

private:
    NodeHandle mBody{};
    NodeHandle mHead{};
    glm::vec3 mPos{0.0f};
    // Where the character was at the previous fixed step. present() renders
    // between the two, so a 60 Hz simulation stays smooth on a 240 Hz display.
    glm::vec3 mPrevPos{0.0f};
    DashTuning mDash;
    glm::vec2 mDashDirection{0.0f};
    float mDashTime = 0.0f;
    float mDashCooldown = 0.0f;
    float mDashRoll = 0.0f;
    float mPrevDashRoll = 0.0f;
    float mDashRollSign = 1.0f;
    float mDashCameraDrop = 0.0f;
    glm::vec3 mPrevHeadOffset{0.0f, 1.7f, 0.0f};
    glm::vec3 mMin{0.0f};
    glm::vec3 mMax{0.0f};
    float mYaw = 0.0f;
    float mPitch = 0.0f;
    float mSpeed = 3.0f;
    float mSens = 0.002f;
    glm::vec2 mVelocity{0.0f};
    glm::vec2 mSlideDirection{0.0f};
    float mVerticalVelocity = 0.0f;
    float mSprintStamina = 1.0f;
    float mSlideTime = 0.0f;
    float mBobPhase = 0.0f;
    float mEyeHeight = 1.7f;
    float mCollisionRadius = 0.30f;
    glm::vec3 mHeadOffset{0.0f, 1.7f, 0.0f};
    float mBaseFov = 70.0f;
    float mLastAppliedFov = 70.0f;
    glm::vec3 mShakeOffset{0.0f};
    glm::vec3 mShakeRotationDegrees{0.0f};
    float mFovKick = 0.0f;
    float mSprintFovKick = 4.0f; // degrees added at full sprint
    float mBobAmount = 0.025f;   // head-bob vertical amplitude (m)
    float mBobSpeed = 8.5f;      // head-bob base cycle speed
    float mCoyoteTime = 0.0f;
    float mJumpBufferTime = 0.0f;
    bool mCrouched = false;
    bool mLastCrouch = false;
    bool mSprinting = false;
    bool mSprintExhausted = false;
    bool mSliding = false;
    bool mCharGrounded = false;
    glm::vec3 mGroundNormal{0,1,0};
    float mCeilingHeight = std::numeric_limits<float>::infinity();
    CollisionMask mCollisionMask = kAllLayers;
    Physics* mPhysics = nullptr;
    CharacterHandle mCharacter{};
};

} // namespace eng
