#include <eng/controllers/FpsController.h>

#include <eng/Input.h>
#include <eng/Physics.h>
#include <eng/Renderer.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kEyeHeight = 1.7f;
constexpr float kCrouchEyeHeight = 1.18f;
// Snappy, high-agility locomotion. The numbers that decide *how* snappy now
// live in eng::MovementTuning and arrive from data; what stays here is the
// shape of the model, which is the part a designer cannot retune anyway.
//
// The air model is Doom/Heretic-like, NOT Quake-like: airborne input steers the
// velocity toward a clamped target at airAcceleration, so air speed can never
// exceed ground speed and there is no strafe-jump acceleration to discover.
// That is a deliberate choice for this game -- "predictable air movement" is
// worth more here than a speed technique, and Quake's model is a different
// velocity integrator rather than a different constant, so it could not be
// reached by tuning these anyway. See docs/fps-gameplay.md.
//
// Generous, sustained sprint: ~10 s at full tilt, ~2 s to fully recover. The
// hysteresis band (drop at empty, resume only past kSprintRecoverThreshold)
// turns the old stamina flap into one long sprint / brief breather / sprint.
constexpr float kStaminaDrain = 0.10f;
constexpr float kStaminaRecover = 0.50f;
constexpr float kSprintStartThreshold = 0.10f;
constexpr float kSprintRecoverThreshold = 0.45f; // auto-resume past this
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

float smootherstep(float value)
{
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}
} // namespace

namespace eng {

bool validMovementTuning(const MovementTuning& t)
{
    const float positive[] = {t.moveSpeed,   t.groundAcceleration,
                              t.groundFriction, t.airAcceleration,
                              t.jumpVelocity, t.gravity};
    for (const float value : positive)
        if (!std::isfinite(value) || value <= 0.0f)
            return false;
    const float nonNegative[] = {t.sprintMultiplier, t.walkMultiplier,
                                 t.crouchMultiplier, t.coyoteTime,
                                 t.jumpBufferTime};
    for (const float value : nonNegative)
        if (!std::isfinite(value) || value < 0.0f)
            return false;
    return true;
}

bool FpsController::setMovementTuning(const MovementTuning& tuning)
{
    // All or nothing. A tuning arrives from a TOML file or a slider, and a
    // half-applied one -- new acceleration against the old friction -- is a
    // feel nobody authored and nobody can reproduce.
    if (!validMovementTuning(tuning))
        return false;
    mMove = tuning;
    return true;
}

glm::vec3 FpsController::eyePosition() const
{
    // The head offset is expressed in the body's frame, so reproduce that
    // rotation here. This keeps interaction raycasts aligned with the actual
    // head -- and therefore with the carried light -- while crouching/bobbing.
    // Deliberately the character's head in both camera shapes: in third person
    // the eye is metres behind on a boom, and aiming from there would let the
    // player shoot through the wall they are standing against.
    const float c = std::cos(mFacingYaw);
    const float s = std::sin(mFacingYaw);
    return mPos + glm::vec3(c * mHeadOffset.x + s * mHeadOffset.z,
                            mHeadOffset.y,
                            -s * mHeadOffset.x + c * mHeadOffset.z);
}

glm::vec3 FpsController::forward() const
{
    const float cp = std::cos(mPitch);
    return {-std::sin(mYaw) * cp, std::sin(mPitch), -std::cos(mYaw) * cp};
}

void FpsController::setViewAngles(float yawRadians, float pitchRadians)
{
    const glm::vec2 limits = cameraRig().pitchLimitsRadians();
    mYaw = yawRadians;
    mPitch = glm::clamp(pitchRadians, limits.x, limits.y);
    if (mFacing == Facing::View) {
        mFacingYaw = yawRadians;
        mPrevFacingYaw = yawRadians;
    }
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
    cameraRig().attach(r);
}

void FpsController::setCameraRig(eng::Renderer& r, CameraRig* rig)
{
    CameraRig* next = rig ? rig : &mDefaultRig;
    if (next == &cameraRig())
        return;
    // Order matters: the outgoing rig drops its nodes before the incoming one
    // takes the camera, so the camera is never parented to a node that is about
    // to be destroyed.
    cameraRig().detach(r);
    mRig = rig;
    cameraRig().attach(r);
    // The view angles are the controller's, not the rig's, so the new shape
    // starts looking where the old one left off -- but its pitch range may be
    // narrower, and an angle outside it would be unwindable.
    const glm::vec2 limits = cameraRig().pitchLimitsRadians();
    mPitch = glm::clamp(mPitch, limits.x, limits.y);
}

void FpsController::reset(glm::vec3 startPos, float speed, float sensitivity,
                          glm::vec3 roomMin, glm::vec3 roomMax, float baseFov)
{
    mPos = startPos;
    mMove.moveSpeed = speed;
    mSens = sensitivity;
    mMin = roomMin;
    mMax = roomMax;
    mVelocity = glm::vec2(0.0f);
    mSlideDirection = glm::vec2(0.0f);
    mVerticalVelocity = 0.0f;
    mSprintStamina = 1.0f;
    mSlideTime = 0.0f;
    mDashTime = 0.0f;
    mDashCooldown = 0.0f;
    mDashRoll = 0.0f;
    mPrevDashRoll = 0.0f;
    mDashRollSign = 1.0f;
    mDashCameraDrop = 0.0f;
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
    mCoyoteTime = mMove.coyoteTime;
    mJumpBufferTime = 0.0f;
    mFacingYaw = mYaw;
    mPrevFacingYaw = mYaw;
    mFacingTarget.reset();
    mLandingImpact = 0.0f;
}

void FpsController::setBaseFov(float degrees)
{
    mBaseFov = degrees;
    mLastAppliedFov = degrees;
}

void FpsController::setDashTuning(const DashTuning& tuning)
{
    mDash = tuning;
    if (!std::isfinite(mDash.speed) || mDash.speed <= 0.0f)
        mDash.speed = 14.0f;
    if (!std::isfinite(mDash.duration) || mDash.duration <= 0.0f)
        mDash.duration = 0.32f;
    if (!std::isfinite(mDash.cooldown) || mDash.cooldown < 0.0f)
        mDash.cooldown = 0.45f;
    if (!std::isfinite(mDash.cameraDrop) || mDash.cameraDrop < 0.0f)
        mDash.cameraDrop = 0.34f;
    if (!std::isfinite(mDash.cameraRollDegrees))
        mDash.cameraRollDegrees = 360.0f;
    mDash.cameraRollDegrees =
        std::round(mDash.cameraRollDegrees / 360.0f) * 360.0f;
}

FpsController::Command FpsController::readCommand(eng::Input& in)
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
    return command;
}

void FpsController::update(eng::Input& in, eng::Renderer& r, float dt)
{
    const Command command = readCommand(in);
    applyLook(command);
    simulate(command, dt);
    present(r, 1.0f, dt);
}

glm::vec2 FpsController::inputDirection(const Command& command) const
{
    const glm::vec3 fwd(-std::sin(mYaw), 0.0f, -std::cos(mYaw));
    const glm::vec3 right(std::cos(mYaw), 0.0f, -std::sin(mYaw));
    const glm::vec3 move = fwd * command.move.y + right * command.move.x;
    if (glm::length(move) <= 0.0f)
        return {0.0f, 0.0f};
    const glm::vec3 unit = glm::normalize(move);
    return {unit.x, unit.z};
}

bool FpsController::beginDash(glm::vec2 direction)
{
    if (mDashTime > 0.0f || mDashCooldown > 0.0f)
        return false;
    if (glm::length(direction) <= 0.0001f) {
        // Neutral input dashes backwards: the backstep. Doing nothing instead
        // would eat the input and the stamina the caller is about to spend.
        const glm::vec3 fwd(-std::sin(mYaw), 0.0f, -std::cos(mYaw));
        direction = {-fwd.x, -fwd.z};
    }
    mDashDirection = glm::normalize(direction);
    const glm::vec2 localRight(std::cos(mYaw), -std::sin(mYaw));
    mDashRollSign = glm::dot(mDashDirection, localRight) < -0.1f ? -1.0f
                                                                 : 1.0f;
    // Previous completed roll is equivalent to identity at 2*pi, but scalar
    // interpolation is not. Reset both endpoints before starting next roll.
    mDashRoll = 0.0f;
    mPrevDashRoll = 0.0f;
    mDashTime = mDash.duration;
    mDashCooldown = mDash.duration + mDash.cooldown;
    // A dash cancels a slide; both own horizontal velocity and the dash wins.
    mSliding = false;
    mSlideTime = 0.0f;
    return true;
}

void FpsController::applyLook(const Command& command)
{
    if (!command.mouseLook)
        return;
    // The rig owns the pitch range -- a neck and a boom are not constrained by
    // the same thing -- and the controller enforces it on the authoritative
    // angle, so pushing past the limit never banks an angle to unwind.
    const glm::vec2 limits = cameraRig().pitchLimitsRadians();
    mYaw -= command.lookDelta.x * mSens;
    mPitch = glm::clamp(mPitch - command.lookDelta.y * mSens, limits.x,
                        limits.y);
}

void FpsController::simulate(const Command& command, float dt)
{
    mPrevPos = mPos;
    mPrevHeadOffset = mHeadOffset;
    mPrevDashRoll = mDashRoll;
    mPrevFacingYaw = mFacingYaw;
    const bool wasGrounded = grounded();
    // Downward speed on the way in, kept because both movement paths zero it
    // the moment they notice the floor -- and the speed it was zeroed *from* is
    // exactly what a landing is worth.
    float fallSpeed = 0.0f;

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
    // Exhaustion latch clears on release OR once stamina climbs back through
    // the hysteresis band. A held sprint therefore auto-resumes after a short
    // breather instead of flapping on/off near a single threshold (the old
    // stop/start stutter). The wide band (empty -> 45%) makes re-triggering
    // impossible until a meaningful chunk has recovered.
    if (!sprintHeld || mSprintStamina >= kSprintRecoverThreshold)
        mSprintExhausted = false;

    mCrouched = command.crouch || mSliding;
    const bool wantsSprint = sprintHeld && hasMove && !mCrouched &&
                             !mSprintExhausted;
    mSprinting = wantsSprint && mSprintStamina >= kSprintStartThreshold;
    if (mSprinting) {
        mSprintStamina = std::max(0.0f, mSprintStamina - kStaminaDrain * dt);
        // Latch exhaustion the moment stamina reaches the start threshold --
        // not at zero. Sprint already cuts off below the threshold, so
        // waiting for empty never fires and the toggle flaps at the boundary.
        if (mSprintStamina <= kSprintStartThreshold) {
            mSprintExhausted = true;
            mSprinting = false;
        }
    } else {
        mSprintStamina = std::min(1.0f, mSprintStamina + kStaminaRecover * dt);
    }

    const bool dashWasActive = mDashTime > 0.0f;
    mDashCooldown = std::max(0.0f, mDashCooldown - dt);
    if (dashWasActive)
        mDashTime = std::max(0.0f, mDashTime - dt);

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
        mJumpBufferTime = mMove.jumpBufferTime;
    else
        mJumpBufferTime = std::max(0.0f, mJumpBufferTime - dt);
    if (grounded())
        mCoyoteTime = mMove.coyoteTime;
    else
        mCoyoteTime = std::max(0.0f, mCoyoteTime - dt);
    if (mJumpBufferTime > 0.0f && mCoyoteTime > 0.0f && !mCrouched) {
        mVerticalVelocity = mMove.jumpVelocity;
        mCoyoteTime = 0.0f;
        mJumpBufferTime = 0.0f;
    }
    float speedFactor = 1.0f;
    if (mSliding) speedFactor = kSlideMultiplier;
    else if (mCrouched) speedFactor = mMove.crouchMultiplier;
    else if (mSprinting) speedFactor = mMove.sprintMultiplier;
    else if (command.walk) speedFactor = mMove.walkMultiplier;

    const float slideProgress = mSlideTime / kSlideDuration;
    // A dash overrides steering entirely: fixed direction, fixed speed, for its
    // whole duration. That is what makes it a commitment rather than a faster
    // way to walk, and it is the half of the souls dodge that the i-frames are
    // paying for.
    const glm::vec2 target =
        dashing() ? mDashDirection * mDash.speed
        : mSliding
            ? mSlideDirection * (mMove.moveSpeed * speedFactor * (0.35f + 0.65f * slideProgress))
            : (hasMove ? moveDirection * (mMove.moveSpeed * speedFactor) : glm::vec2(0.0f));
    // Reaching dash speed has to be instant; ramping it turns the escape into a
    // lean.
    constexpr float kDashAcceleration = 1000.0f;
    const float rate = dashing() ? kDashAcceleration
                     : mSliding  ? kSlideDeceleration
                                 : (!grounded() ? mMove.airAcceleration
                                                : (hasMove ? mMove.groundAcceleration
                                                           : mMove.groundFriction));
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
        mVerticalVelocity += mPhysics->gravityY() * dt;
        constexpr float kTerminalVelocity = -50.0f;
        if (mVerticalVelocity < kTerminalVelocity)
            mVerticalVelocity = kTerminalVelocity;

        fallSpeed = mVerticalVelocity;
        const glm::vec3 vel(mVelocity.x, mVerticalVelocity, mVelocity.y);
        mPhysics->characterSetVelocity(mCharacter, vel);
        mPhysics->characterUpdate(mCharacter, dt, mCollisionMask);
        const eng::CharacterState st = mPhysics->characterState(mCharacter);
        mGroundNormal = st.groundNormal;

        const glm::vec3 beforeMove = mPos;
        mPos = st.position; // feet position
        mCharGrounded = st.grounded();
        if (mCharGrounded && mVerticalVelocity < 0.0f)
            mVerticalVelocity = 0.0f;

        // Ceiling clamp: keep camera from poking through the roof mesh.
        const float maxFeetY = mCeilingHeight - mEyeHeight - kCeilingClearance;
        if (mPos.y > maxFeetY) {
            mPos.y = maxFeetY;
            mVerticalVelocity = 0.0f;
        }

        // Feed actual displacement back for bob/FOV.
        mVelocity = glm::vec2((mPos.x - beforeMove.x) / safeDt,
                               (mPos.z - beforeMove.z) / safeDt);
    } else {
        // No physics (test fallback): manual gravity + AABB clamp.
        fallSpeed = mVerticalVelocity;
        if (!grounded() || mVerticalVelocity > 0.0f) {
            mVerticalVelocity -= mMove.gravity * dt;
            mPos.y += mVerticalVelocity * dt;
            // Apply the optional game-supplied ceiling constraint.
            const float maxFeetY = mCeilingHeight - mEyeHeight - kCeilingClearance;
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

    // Dash roll is presentation only: capsule/steering stay owned by movement.
    // Drop follows a smooth arch while roll completes exactly one turn, so the
    // camera returns to identity without an orientation snap.
    if (dashWasActive) {
        const float duration = std::max(mDash.duration, 0.001f);
        const float progress =
            std::clamp(1.0f - mDashTime / duration, 0.0f, 1.0f);
        mDashCameraDrop = std::max(0.0f, mDash.cameraDrop) *
                          std::sin(glm::pi<float>() * progress);
        mDashRoll = mDashRollSign *
                    glm::radians(mDash.cameraRollDegrees) *
                    smootherstep(progress);
    } else {
        mDashCameraDrop = 0.0f;
    }

    // Touchdown, for the camera's landing dip. Recorded rather than acted on:
    // what a landing looks like is the rig's business, and only the simulation
    // knows the frame it happened on.
    if (!wasGrounded && grounded())
        mLandingImpact = std::max(mLandingImpact, -std::min(0.0f, fallSpeed));

    // Where the body points. In first person there is nothing to decide -- the
    // body is the view. In third person it turns towards a lock-on target if
    // there is one, else towards travel, and holds its last facing when the
    // player stops, so standing still does not snap the character to north.
    if (mFacing == Facing::View) {
        mFacingYaw = mYaw;
    } else {
        float desired = mFacingYaw;
        if (mFacingTarget) {
            const glm::vec3 delta = *mFacingTarget - mPos;
            const glm::vec3 flat(delta.x, 0.0f, delta.z);
            if (glm::length(flat) > 1e-3f)
                desired = yawOfDirection(glm::normalize(flat));
        } else if (glm::length(mVelocity) > 0.05f) {
            desired = yawOfDirection(glm::normalize(
                glm::vec3(mVelocity.x, 0.0f, mVelocity.y)));
        }
        mFacingYaw = turnTowards(mFacingYaw, desired,
                                 glm::radians(mTurnRateDegrees) * dt);
        // Interpolating across the wrap would spin the character the long way
        // round once per revolution; keep both ends on the same branch.
        if (std::abs(mFacingYaw - mPrevFacingYaw) > glm::pi<float>())
            mPrevFacingYaw = mFacingYaw;
    }

    // The authored templates pair locomotion state with camera feedback.
    const float horizontalSpeed = glm::length(mVelocity);
    const float speedRatio = glm::clamp(horizontalSpeed / std::max(mMove.moveSpeed, 0.001f),
                                        0.0f, mMove.sprintMultiplier);
    const bool actuallyMoving = horizontalSpeed > 0.01f;
    mBobPhase += dt * (actuallyMoving ? (mBobSpeed + speedRatio * 3.0f) : 0.0f);
    const float targetEye = mCrouched ? kCrouchEyeHeight : kEyeHeight;
    mEyeHeight = approach(mEyeHeight, targetEye, 3.4f * dt);
    const float bobAmount = actuallyMoving ? mBobAmount * speedRatio : 0.0f;
    mHeadOffset = {
        std::sin(mBobPhase * 0.5f) * bobAmount * 0.55f,
        mEyeHeight + std::abs(std::sin(mBobPhase)) * bobAmount -
            mDashCameraDrop,
        0.0f};
    mFovKick = mSprinting ? mSprintFovKick * speedRatio : 0.0f;
}

void FpsController::present(eng::Renderer& r, float alpha, float dt)
{
    // The debug camera panel can revise the locomotion base FOV. FOV stays
    // here rather than moving into a rig: there is one camera, one lens, and
    // sprint feedback means the same thing whichever shape is framing it.
    const float rendererFov = r.envState().fovDeg;
    if (std::abs(rendererFov - mLastAppliedFov) > 0.001f)
        mBaseFov = rendererFov;
    const float desiredFov = mBaseFov + mFovKick;
    if (std::abs(rendererFov - desiredFov) > 0.001f)
        r.setCameraFov(desiredFov);
    mLastAppliedFov = desiredFov;

    const float t = glm::clamp(alpha, 0.0f, 1.0f);
    // Everything the camera is allowed to know, interpolated between the last
    // two fixed steps. The simulated values are left alone: aiming, collision
    // and the muzzle all read a steady pose while the view moves.
    CameraPose pose;
    pose.focus = glm::mix(mPrevPos, mPos, t);
    pose.headOffset = glm::mix(mPrevHeadOffset, mHeadOffset, t);
    pose.viewYaw = mYaw;
    pose.viewPitch = mPitch;
    pose.facingYaw = mFacing == Facing::View
                         ? mYaw
                         : mPrevFacingYaw +
                               wrapAngle(mFacingYaw - mPrevFacingYaw) * t;
    pose.rollRadians = glm::mix(mPrevDashRoll, mDashRoll, t);
    const float horizontalSpeed = glm::length(mVelocity);
    pose.speedRatio =
        glm::clamp(horizontalSpeed / std::max(mMove.moveSpeed, 0.001f), 0.0f, 1.0f);
    // Signed lateral fraction, in the *view's* frame: which way the body is
    // sliding across the screen is what a lean and a boom lag react to, not
    // which way it is travelling in the world.
    if (horizontalSpeed > 0.01f) {
        const glm::vec2 right(std::cos(mYaw), -std::sin(mYaw));
        pose.strafeRatio =
            glm::clamp(glm::dot(mVelocity, right) / std::max(mMove.moveSpeed, 0.001f),
                       -1.0f, 1.0f);
    }
    pose.grounded = grounded();
    pose.landingImpact = mLandingImpact;
    mLandingImpact = 0.0f; // a one-frame event, consumed exactly once

    CameraRig& rig = cameraRig();
    rig.present(r, pose, dt);
    // A rig that frames a target owns the view angles while it does. Adopting
    // them here is what keeps movement camera-relative under a lock-on and what
    // makes releasing one continuous instead of a snap back to the mouse.
    float overrideYaw = mYaw;
    float overridePitch = mPitch;
    if (rig.viewOverride(overrideYaw, overridePitch)) {
        mYaw = overrideYaw;
        mPitch = overridePitch;
    }
}

} // namespace eng
