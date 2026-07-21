#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

namespace eng {
class Input;
class Physics;
class Renderer;
} // namespace eng

// Grounded first-person controller inspired by the Godot FPS templates:
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
    void simulate(const Command& command, float dt);
    void present(eng::Renderer& r);
    void update(eng::Input& in, eng::Renderer& r, float dt);

    float& speed() { return mSpeed; }
    float& sensitivity() { return mSens; }
    float sprintStamina() const { return mSprintStamina; } // normalized 0..1
    bool crouched() const { return mCrouched; }
    bool sprinting() const { return mSprinting; }
    bool sliding() const { return mSliding; }
    bool grounded() const { return mPhysics ? mCharGrounded : (mPos.y <= 0.001f); }
    glm::vec3 groundNormal() const { return mGroundNormal; }
    float horizontalSpeed() const { return glm::length(mVelocity); }
    glm::vec3 position() const { return mPos; }
    // Horizontal capsule footprint used by the dungeon's rendered-wall sweep.
    // Kept below the 0.8 m half-width arch opening for comfortable traversal.
    float collisionRadius() const { return mCollisionRadius; }
    // Keeps locomotion feedback separate from a designer/debug-camera FOV.
    float baseFov() const { return mBaseFov; }
    void setBaseFov(float degrees);

    // Eye position (feet + eye height) and view direction, for interaction
    // ray checks.
    glm::vec3 eyePosition() const;
    glm::vec3 forward() const;

    // Head node (camera parent), e.g. for attaching a player-carried light.
    eng::NodeHandle headNode() const { return mHead; }

private:
    eng::NodeHandle mBody{};
    eng::NodeHandle mHead{};
    glm::vec3 mPos{0.0f};
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
    float mFovKick = 0.0f;
    float mCoyoteTime = 0.0f;
    float mJumpBufferTime = 0.0f;
    bool mCrouched = false;
    bool mLastCrouch = false;
    bool mSprinting = false;
    bool mSprintExhausted = false;
    bool mSliding = false;
    bool mCharGrounded = false;
    glm::vec3 mGroundNormal{0,1,0};
    eng::Physics* mPhysics = nullptr;
    eng::CharacterHandle mCharacter{};
};
