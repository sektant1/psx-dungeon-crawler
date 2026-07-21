#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <functional>

namespace eng {
class Input;
class Renderer;
} // namespace eng

// Grounded first-person controller inspired by the Godot FPS templates:
// acceleration/deceleration, sprint stamina, hold-to-crouch and restrained
// camera bob/FOV feedback. Yaw lives on the body, pitch on the head.
class FpsController
{
public:
    // Resolves a desired move against level geometry; returns the allowed
    // position (e.g. DungeonMap::resolveMove). Replaces the AABB clamp.
    using MoveResolver = std::function<glm::vec3(glm::vec3 from, glm::vec3 to)>;

    void init(eng::Renderer& r, glm::vec3 startPos, float speed,
              float sensitivity, glm::vec3 roomMin, glm::vec3 roomMax);
    void setResolver(MoveResolver resolver) { mResolve = std::move(resolver); }
    void update(eng::Input& in, eng::Renderer& r, float dt);

    float& speed() { return mSpeed; }
    float& sensitivity() { return mSens; }
    float sprintStamina() const { return mSprintStamina; } // normalized 0..1
    bool crouched() const { return mCrouched; }
    bool sprinting() const { return mSprinting; }
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
    float mSprintStamina = 1.0f;
    float mBobPhase = 0.0f;
    float mEyeHeight = 1.7f;
    glm::vec3 mHeadOffset{0.0f, 1.7f, 0.0f};
    float mBaseFov = 70.0f;
    float mLastAppliedFov = 70.0f;
    bool mCrouched = false;
    bool mSprinting = false;
    MoveResolver mResolve;
};
