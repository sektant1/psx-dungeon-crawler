#include "FpsController.h"

#include <eng/Input.h>
#include <eng/Physics.h>
#include <eng/Renderer.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kEyeHeight = 1.7f;
constexpr float kCrouchEyeHeight = 1.18f;
const float kMaxPitch = glm::radians(89.0f);
constexpr float kAcceleration = 24.0f;
constexpr float kDeceleration = 30.0f;
constexpr float kAirAcceleration = 8.5f;
constexpr float kSprintMultiplier = 1.7f;
constexpr float kWalkMultiplier = 0.55f;
constexpr float kCrouchMultiplier = 0.45f;
constexpr float kStaminaDrain = 0.28f; // full sprint lasts ~3.6 seconds
constexpr float kStaminaRecover = 0.18f;
constexpr float kSprintStartThreshold = 0.08f;
constexpr float kJumpVelocity = 5.0f;
constexpr float kGravity = 18.0f;
constexpr float kCoyoteDuration = 0.10f;
constexpr float kJumpBufferDuration = 0.12f;
constexpr float kDungeonCeilingY = 3.0f;
constexpr float kCeilingClearance = 0.10f;
constexpr float kSlideDuration = 0.55f;
constexpr float kSlideMultiplier = 2.0f;
constexpr float kSlideDeceleration = 18.0f;

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

void FpsController::init(eng::Renderer& r, eng::Physics& physics,
                         glm::vec3 startPos, float speed, float sensitivity,
                         glm::vec3 roomMin, glm::vec3 roomMax)
{
    mPhysics = &physics;
    reset(startPos, speed, sensitivity, roomMin, roomMax, r.envState().fovDeg);
    // Remove any previous character (level transitions call init repeatedly).
    if (mCharacter.valid())
        physics.removeCharacter(mCharacter);
    eng::CharacterDesc cd;
    cd.position = startPos;
    cd.radius = mCollisionRadius;
    cd.height = 1.7f;
    mCharacter = physics.createCharacter(cd);
    mCharGrounded = false;
    mLastCrouch = false;
    mBody = r.createNode(eng::kRootNode, mPos);
    mHead = r.createNode(mBody, {0.0f, kEyeHeight, 0.0f});
    r.attachCamera(mHead);
}

void FpsController::reset(glm::vec3 startPos, float speed, float sensitivity,
                          glm::vec3 roomMin, glm::vec3 roomMax, float baseFov)
{
    mPos = startPos;
    mSpeed = speed;
    mSens = sensitivity;
    mMin = roomMin;
    mMax = roomMax;
    mVelocity = glm::vec2(0.0f);
    mSlideDirection = glm::vec2(0.0f);
    mVerticalVelocity = 0.0f;
    mSprintStamina = 1.0f;
    mSlideTime = 0.0f;
    mBobPhase = 0.0f;
    mEyeHeight = kEyeHeight;
    mCollisionRadius = 0.30f;
    mHeadOffset = {0.0f, kEyeHeight, 0.0f};
    mBaseFov = baseFov;
    mLastAppliedFov = mBaseFov;
    mFovKick = 0.0f;
    mCrouched = false;
    mSprinting = false;
    mSprintExhausted = false;
    mSliding = false;
    mCoyoteTime = kCoyoteDuration;
    mJumpBufferTime = 0.0f;
}

void FpsController::setBaseFov(float degrees)
{
    mBaseFov = degrees;
    mLastAppliedFov = degrees;
}

void FpsController::update(eng::Input& in, eng::Renderer& r, float dt)
{
    Command command;
    command.mouseLook = in.mouseGrabbed();
    command.lookDelta = in.mouseDelta();
    command.move.x = float(in.isDown("move_right")) - float(in.isDown("move_left"));
    command.move.y = float(in.isDown("move_forward")) - float(in.isDown("move_back"));
    command.sprint = in.isDown("sprint");
    command.walk = in.isDown("walk");
    command.crouch = in.isDown("crouch");
    command.jumpPressed = in.wasPressed("jump");
    command.slidePressed = in.wasPressed("slide");
    simulate(command, dt);
    present(r);
}

void FpsController::simulate(const Command& command, float dt)
{
    if (command.mouseLook) {
        mYaw -= command.lookDelta.x * mSens;
        mPitch = glm::clamp(mPitch - command.lookDelta.y * mSens,
                            -kMaxPitch, kMaxPitch);
    }

    // Camera looks down -Z at yaw 0; forward/right on the ground plane.
    const glm::vec3 fwd(-std::sin(mYaw), 0.0f, -std::cos(mYaw));
    const glm::vec3 right(std::cos(mYaw), 0.0f, -std::sin(mYaw));
    glm::vec3 move(0.0f);
    move += fwd * command.move.y;
    move += right * command.move.x;
    const bool hasMove = glm::length(move) > 0.0f;
    const glm::vec2 moveDirection = hasMove
        ? glm::vec2(glm::normalize(move).x, glm::normalize(move).z)
        : glm::vec2(0.0f);
    const bool sprintHeld = command.sprint;
    if (!sprintHeld)
        mSprintExhausted = false;

    mCrouched = command.crouch || mSliding;
    const bool wantsSprint = sprintHeld && hasMove && !mCrouched &&
                             !mSprintExhausted;
    mSprinting = wantsSprint && mSprintStamina >= kSprintStartThreshold;
    if (mSprinting) {
        mSprintStamina = std::max(0.0f, mSprintStamina - kStaminaDrain * dt);
        // Latch exhaustion at the same threshold used to start sprinting.
        // Previously the controller stopped at 8%, recovered just above 8%,
        // sprinted one frame, then stopped again: a visible speed/FOV/bob
        // stutter for as long as Shift remained held.
        if (mSprintStamina < kSprintStartThreshold) {
            mSprintExhausted = true;
            mSprinting = false;
        }
    } else {
        mSprintStamina = std::min(1.0f, mSprintStamina + kStaminaRecover * dt);
    }

    if (!mSliding && command.slidePressed && mSprinting && grounded()) {
        mSliding = true;
        mSlideTime = kSlideDuration;
        mSlideDirection = moveDirection;
        mSprinting = false;
    }
    if (mSliding) {
        mSlideTime = std::max(0.0f, mSlideTime - dt);
        if (mSlideTime <= 0.0f)
            mSliding = false;
    }
    mCrouched = command.crouch || mSliding;

    if (command.jumpPressed)
        mJumpBufferTime = kJumpBufferDuration;
    else
        mJumpBufferTime = std::max(0.0f, mJumpBufferTime - dt);
    if (grounded())
        mCoyoteTime = kCoyoteDuration;
    else
        mCoyoteTime = std::max(0.0f, mCoyoteTime - dt);
    if (mJumpBufferTime > 0.0f && mCoyoteTime > 0.0f && !mCrouched) {
        mVerticalVelocity = kJumpVelocity;
        mCoyoteTime = 0.0f;
        mJumpBufferTime = 0.0f;
    }
    float speedFactor = 1.0f;
    if (mSliding) speedFactor = kSlideMultiplier;
    else if (mCrouched) speedFactor = kCrouchMultiplier;
    else if (mSprinting) speedFactor = kSprintMultiplier;
    else if (command.walk) speedFactor = kWalkMultiplier;

    const float slideProgress = mSlideTime / kSlideDuration;
    const glm::vec2 target = mSliding
        ? mSlideDirection * (mSpeed * speedFactor * (0.35f + 0.65f * slideProgress))
        : (hasMove ? moveDirection * (mSpeed * speedFactor) : glm::vec2(0.0f));
    const float rate = mSliding ? kSlideDeceleration
                                : (!grounded() ? kAirAcceleration
                                               : (hasMove ? kAcceleration
                                                          : kDeceleration));
    mVelocity.x = approach(mVelocity.x, target.x, rate * dt);
    mVelocity.y = approach(mVelocity.y, target.y, rate * dt);

    const float safeDt = std::max(dt, 1e-4f);
    if (mPhysics && mCharacter.valid()) {
        // Crouch shape change: only on transition, not every frame.
        if (mCrouched != mLastCrouch) {
            mPhysics->characterSetShape(mCharacter, mCollisionRadius,
                                        mCrouched ? 1.2f : 1.7f);
            mLastCrouch = mCrouched;
        }

        // Gravity integration: apply gravity each step; if grounded and
        // falling, clamp to 0 so the character sticks to the floor.
        // Jolt's ExtendedUpdate integrates the velocity we set — we must
        // NOT also manually integrate mPos.y to avoid double-gravity.
        if (mCharGrounded && mVerticalVelocity < 0.0f)
            mVerticalVelocity = 0.0f;
        mVerticalVelocity -= kGravity * dt;
        constexpr float kTerminalVelocity = -50.0f;
        if (mVerticalVelocity < kTerminalVelocity)
            mVerticalVelocity = kTerminalVelocity;

        const glm::vec3 vel(mVelocity.x, mVerticalVelocity, mVelocity.y);
        mPhysics->characterSetVelocity(mCharacter, vel);
        mPhysics->characterUpdate(mCharacter, dt);
        const eng::CharacterState st = mPhysics->characterState(mCharacter);

        const glm::vec3 beforeMove = mPos;
        mPos = st.position; // feet position
        mCharGrounded = st.grounded();
        if (mCharGrounded && mVerticalVelocity < 0.0f)
            mVerticalVelocity = 0.0f;

        // Ceiling clamp: keep camera from poking through the roof mesh.
        const float maxFeetY = kDungeonCeilingY - mEyeHeight - kCeilingClearance;
        if (mPos.y > maxFeetY) {
            mPos.y = maxFeetY;
            mVerticalVelocity = 0.0f;
        }

        // Feed actual displacement back for bob/FOV.
        mVelocity = glm::vec2((mPos.x - beforeMove.x) / safeDt,
                               (mPos.z - beforeMove.z) / safeDt);
    } else {
        // No physics (test fallback): manual gravity + AABB clamp.
        if (!grounded() || mVerticalVelocity > 0.0f) {
            mVerticalVelocity -= kGravity * dt;
            mPos.y += mVerticalVelocity * dt;
            // Every dungeon tile renders its roof at y=3. Keep the camera below
            // it rather than allowing a jump to pass through visible geometry.
            const float maxFeetY = kDungeonCeilingY - mEyeHeight - kCeilingClearance;
            if (mPos.y > maxFeetY) {
                mPos.y = maxFeetY;
                mVerticalVelocity = 0.0f;
            }
            if (mPos.y <= 0.0f) {
                mPos.y = 0.0f;
                mVerticalVelocity = 0.0f;
            }
        }

        const glm::vec3 beforeMove = mPos;
        const glm::vec3 desired =
            mPos + glm::vec3(mVelocity.x, 0.0f, mVelocity.y) * dt;
        mPos = glm::clamp(desired, mMin, mMax);
        // Collision owns the final movement. Feeding its actual displacement back
        // into locomotion eliminates phantom speed/bob while pushing a wall and
        // makes changing direction after a collision responsive.
        mVelocity = glm::vec2((mPos.x - beforeMove.x) / safeDt,
                               (mPos.z - beforeMove.z) / safeDt);
    }

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
    mFovKick = mSprinting ? 4.0f * speedRatio : 0.0f;
}

void FpsController::present(eng::Renderer& r)
{
    // The debug camera panel can revise the locomotion base FOV.
    const float rendererFov = r.envState().fovDeg;
    if (std::abs(rendererFov - mLastAppliedFov) > 0.001f)
        mBaseFov = rendererFov;
    const float desiredFov = mBaseFov + mFovKick;
    if (std::abs(rendererFov - desiredFov) > 0.001f)
        r.setCameraFov(desiredFov);
    mLastAppliedFov = desiredFov;

    r.setPosition(mHead, mHeadOffset);
    r.setPosition(mBody, mPos);
    r.setOrientation(mBody, glm::angleAxis(mYaw, glm::vec3(0, 1, 0)));
    r.setOrientation(mHead, glm::angleAxis(mPitch, glm::vec3(1, 0, 0)));
}
