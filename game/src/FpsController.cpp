#include "FpsController.h"

#include <eng/Input.h>
#include <eng/Renderer.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kEyeHeight = 1.7f;
constexpr float kCrouchEyeHeight = 1.18f;
const float kMaxPitch = glm::radians(89.0f);
constexpr float kAcceleration = 24.0f;
constexpr float kDeceleration = 30.0f;
constexpr float kSprintMultiplier = 1.7f;
constexpr float kWalkMultiplier = 0.55f;
constexpr float kCrouchMultiplier = 0.45f;
constexpr float kStaminaDrain = 0.28f; // full sprint lasts ~3.6 seconds
constexpr float kStaminaRecover = 0.18f;

float approach(float value, float target, float maxDelta)
{
    if (value < target)
        return std::min(value + maxDelta, target);
    return std::max(value - maxDelta, target);
}
} // namespace

glm::vec3 FpsController::eyePosition() const
{
    // mHead is parented to the yaw-only body node, so reproduce that local
    // offset here. This keeps interaction raycasts aligned with the actual
    // camera and carried light while crouching/bobbing.
    const float c = std::cos(mYaw);
    const float s = std::sin(mYaw);
    return mPos + glm::vec3(c * mHeadOffset.x + s * mHeadOffset.z,
                            mHeadOffset.y,
                            -s * mHeadOffset.x + c * mHeadOffset.z);
}

glm::vec3 FpsController::forward() const
{
    const float cp = std::cos(mPitch);
    return {-std::sin(mYaw) * cp, std::sin(mPitch), -std::cos(mYaw) * cp};
}

void FpsController::init(eng::Renderer& r, glm::vec3 startPos, float speed,
                         float sensitivity, glm::vec3 roomMin, glm::vec3 roomMax)
{
    mPos = startPos;
    mSpeed = speed;
    mSens = sensitivity;
    mMin = roomMin;
    mMax = roomMax;
    mVelocity = glm::vec2(0.0f);
    mSprintStamina = 1.0f;
    mBobPhase = 0.0f;
    mEyeHeight = kEyeHeight;
    mHeadOffset = {0.0f, kEyeHeight, 0.0f};
    mBaseFov = r.envState().fovDeg;
    mLastAppliedFov = mBaseFov;
    mCrouched = false;
    mSprinting = false;
    mBody = r.createNode(eng::kRootNode, mPos);
    mHead = r.createNode(mBody, {0.0f, kEyeHeight, 0.0f});
    r.attachCamera(mHead);
}

void FpsController::setBaseFov(float degrees)
{
    mBaseFov = degrees;
    mLastAppliedFov = degrees;
}

void FpsController::update(eng::Input& in, eng::Renderer& r, float dt)
{
    if (in.mouseGrabbed()) {
        const glm::vec2 d = in.mouseDelta();
        mYaw -= d.x * mSens;
        mPitch = glm::clamp(mPitch - d.y * mSens, -kMaxPitch, kMaxPitch);
    }

    // Camera looks down -Z at yaw 0; forward/right on the ground plane.
    const glm::vec3 fwd(-std::sin(mYaw), 0.0f, -std::cos(mYaw));
    const glm::vec3 right(std::cos(mYaw), 0.0f, -std::sin(mYaw));
    glm::vec3 move(0.0f);
    if (in.isDown("move_forward"))
        move += fwd;
    if (in.isDown("move_back"))
        move -= fwd;
    if (in.isDown("move_right"))
        move += right;
    if (in.isDown("move_left"))
        move -= right;
    const bool hasMove = glm::length(move) > 0.0f;
    mCrouched = in.isDown("crouch");
    const bool wantsSprint = in.isDown("sprint") && hasMove && !mCrouched;
    mSprinting = wantsSprint && mSprintStamina > 0.0f;
    if (mSprinting)
        mSprintStamina = std::max(0.0f, mSprintStamina - kStaminaDrain * dt);
    else
        mSprintStamina = std::min(1.0f, mSprintStamina + kStaminaRecover * dt);

    float speedFactor = 1.0f;
    if (mCrouched) speedFactor = kCrouchMultiplier;
    else if (mSprinting) speedFactor = kSprintMultiplier;
    else if (in.isDown("walk")) speedFactor = kWalkMultiplier;

    const glm::vec2 target = hasMove
        ? glm::vec2(glm::normalize(move).x, glm::normalize(move).z) *
              (mSpeed * speedFactor)
        : glm::vec2(0.0f);
    const float rate = hasMove ? kAcceleration : kDeceleration;
    mVelocity.x = approach(mVelocity.x, target.x, rate * dt);
    mVelocity.y = approach(mVelocity.y, target.y, rate * dt);
    const glm::vec3 beforeMove = mPos;
    const glm::vec3 desired =
        mPos + glm::vec3(mVelocity.x, 0.0f, mVelocity.y) * dt;
    mPos = mResolve ? mResolve(mPos, desired)
                    : glm::clamp(desired, mMin, mMax);
    // Collision owns the final movement. Feeding its actual displacement back
    // into locomotion eliminates phantom speed/bob while pushing a wall and
    // makes changing direction after a collision responsive.
    const float safeDt = std::max(dt, 1e-4f);
    mVelocity = glm::vec2((mPos.x - beforeMove.x) / safeDt,
                           (mPos.z - beforeMove.z) / safeDt);

    // The authored templates pair locomotion state with camera feedback.
    // Keep it subtle for the PSX presentation: no nausea-inducing roll, just
    // a small local sway and a sprint FOV kick.
    const float horizontalSpeed = glm::length(mVelocity);
    const float speedRatio = glm::clamp(horizontalSpeed / std::max(mSpeed, 0.001f),
                                        0.0f, kSprintMultiplier);
    const bool actuallyMoving = horizontalSpeed > 0.01f;
    mBobPhase += dt * (actuallyMoving ? (8.5f + speedRatio * 3.0f) : 0.0f);
    const float targetEye = mCrouched ? kCrouchEyeHeight : kEyeHeight;
    mEyeHeight = approach(mEyeHeight, targetEye, 3.4f * dt);
    const float bobAmount = actuallyMoving ? 0.025f * speedRatio : 0.0f;
    mHeadOffset = {
        std::sin(mBobPhase * 0.5f) * bobAmount * 0.55f,
        mEyeHeight + std::abs(std::sin(mBobPhase)) * bobAmount,
        0.0f};
    r.setPosition(mHead, mHeadOffset);

    // The renderer's environment cache is also edited by the debug camera
    // panel. If somebody changed FOV since our last write, adopt that as the
    // new base instead of continually forcing 70 degrees.
    const float rendererFov = r.envState().fovDeg;
    if (std::abs(rendererFov - mLastAppliedFov) > 0.001f)
        mBaseFov = rendererFov;
    const float fovKick = mSprinting ? 4.0f * speedRatio : 0.0f;
    mLastAppliedFov = mBaseFov + fovKick;
    r.setCameraFov(mLastAppliedFov);

    r.setPosition(mBody, mPos);
    r.setOrientation(mBody, glm::angleAxis(mYaw, glm::vec3(0, 1, 0)));
    r.setOrientation(mHead, glm::angleAxis(mPitch, glm::vec3(1, 0, 0)));
}
