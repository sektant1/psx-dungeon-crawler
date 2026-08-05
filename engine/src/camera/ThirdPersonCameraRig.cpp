#include <eng/camera/ThirdPersonCameraRig.h>

#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace {

// How far the framing point is allowed to slide off the character towards a
// lock-on target, in metres. Without a cap, a target across the room drags the
// camera off the player entirely and the roll you are about to make happens
// off-screen; with it, distant fights frame the player and near ones frame
// both.
constexpr float kMaxLockFocusShift = 1.35f;
// Distance at which the lock-on boom extension is at half its maximum. A soft
// saturation rather than a range the rig would have to be told: gameplay owns
// how far a lock reaches, and the camera only cares that further means wider.
constexpr float kLockBoostHalfDistance = 6.0f;

glm::vec3 directionFrom(float yaw, float pitch)
{
    const float cp = std::cos(pitch);
    return {-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}

} // namespace

namespace eng {

void ThirdPersonCameraRig::attach(Renderer& r)
{
    // The character node is a sibling of the camera chain, not its parent: the
    // body turns to face where it is going, and a camera parented to it would
    // be dragged round by every step the player took.
    mCharacter = r.createNode(kRootNode);
    mPivot = r.createNode(kRootNode);
    mBoomNode = r.createNode(mPivot);
    mEye = r.createNode(mBoomNode, {0.0f, 0.0f, mTuning.distance});
    r.attachCamera(mEye);
    mSeeded = false;
    mBoomLength = mTuning.distance;
}

void ThirdPersonCameraRig::detach(Renderer& r)
{
    if (mCharacter.valid())
        r.destroyNode(mCharacter);
    if (mPivot.valid())
        r.destroyNode(mPivot); // takes the boom and the eye with it
    mCharacter = {};
    mPivot = {};
    mBoomNode = {};
    mEye = {};
}

glm::vec2 ThirdPersonCameraRig::pitchLimitsRadians() const
{
    return {glm::radians(std::min(mTuning.pitchMinDegrees,
                                  mTuning.pitchMaxDegrees)),
            glm::radians(std::max(mTuning.pitchMinDegrees,
                                  mTuning.pitchMaxDegrees))};
}

glm::vec3 ThirdPersonCameraRig::forward() const
{
    return directionFrom(mYaw, mPitch);
}

bool ThirdPersonCameraRig::viewOverride(float& yaw, float& pitch) const
{
    // Only while a lock is held. Handing the angles back every frame is what
    // makes releasing the lock continuous: the controller's authoritative yaw
    // has been tracking the camera all along, so the view does not jump back to
    // wherever the mouse left it before the lock.
    if (!mLock.active)
        return false;
    yaw = mYaw;
    pitch = mPitch;
    return true;
}

void ThirdPersonCameraRig::snapTo(const CameraPose& pose)
{
    const glm::vec2 limits = pitchLimitsRadians();
    mFocus = pose.focus;
    mYaw = pose.viewYaw;
    mPitch = std::clamp(pose.viewPitch, limits.x, limits.y);
    mBoomLength = mTuning.distance;
    mSeeded = true;
}

ThirdPersonCameraRig::Solution ThirdPersonCameraRig::solve(
    const CameraPose& pose, float dt)
{
    if (!mSeeded)
        snapTo(pose);

    // --- follow ----------------------------------------------------------
    // Horizontal fast, vertical slow. Vertical is where stairs, steps and
    // landings live: tracking them exactly is the classic third-person pumping,
    // and lagging them costs nothing because the character stays in frame
    // regardless of a few centimetres of height error.
    mFocus.x = smoothTowards(mFocus.x, pose.focus.x, mTuning.followRate, dt);
    mFocus.z = smoothTowards(mFocus.z, pose.focus.z, mTuning.followRate, dt);
    mFocus.y = smoothTowards(mFocus.y, pose.focus.y,
                             mTuning.followRateVertical, dt);

    const glm::vec3 anchor = mFocus + glm::vec3(0.0f, mTuning.pivotHeight, 0.0f);
    const glm::vec2 limits = pitchLimitsRadians();

    Solution out;
    out.locked = mLock.active;
    out.look = anchor;
    out.distance = mTuning.distance;

    if (mLock.active) {
        // The boom hangs from the framing point and the camera looks along it,
        // so whatever the pivot is, is dead centre of the screen. That is why
        // the framing bias moves the *pivot* towards the target rather than
        // rotating the camera off the player: rotation would put the player
        // somewhere the rig then has to keep track of.
        const glm::vec3 delta = mLock.point - anchor;
        const glm::vec3 flat(delta.x, 0.0f, delta.z);
        const float flatLength = glm::length(flat);
        if (flatLength > 1e-3f) {
            const float bias = std::clamp(mTuning.lockFramingBias, 0.0f, 1.0f);
            glm::vec3 shift = delta * bias;
            const float shiftLength = glm::length(shift);
            if (shiftLength > kMaxLockFocusShift)
                shift *= kMaxLockFocusShift / shiftLength;
            out.look = anchor + shift;

            const float desiredYaw = yawOfDirection(flat / flatLength);
            // Elevation of the target above the anchor, damped by the same
            // bias: a tall enemy at arm's length should tilt the camera, not
            // point it at the ceiling.
            const float desiredPitch =
                std::clamp(std::atan2(delta.y + mLock.radius * 0.5f,
                                      std::max(flatLength, 0.5f)) *
                                   bias +
                               glm::radians(mTuning.lockPitchDegrees),
                           limits.x, limits.y);
            mYaw = smoothAngleTowards(mYaw, desiredYaw, mTuning.lockBlendRate,
                                      dt);
            mPitch = smoothTowards(mPitch, desiredPitch, mTuning.lockBlendRate,
                                   dt);
            out.distance =
                mTuning.distance +
                mTuning.lockDistanceBoost *
                    (flatLength / (flatLength + kLockBoostHalfDistance));
        }
    } else {
        // Unlocked, the mouse owns the orbit outright. No smoothing at all:
        // easing the angles behind the pointer is input lag, and it is the one
        // place a camera must never add any.
        mYaw = pose.viewYaw;
        mPitch = pose.viewPitch;
    }
    mPitch = std::clamp(mPitch, limits.x, limits.y);
    mLockedLastFrame = mLock.active;

    // The over-the-shoulder bias, in the camera's own frame so it stays on the
    // same side of the screen however the orbit is turned.
    const glm::vec3 right(std::cos(mYaw), 0.0f, -std::sin(mYaw));
    out.pivot = out.look + right * mTuning.shoulderOffset;
    out.yaw = mYaw;
    out.pitch = mPitch;
    return out;
}

float ThirdPersonCameraRig::boomLength(float current, float desired,
                                       float blocked, float dt) const
{
    const float target = std::max(std::min(desired, blocked),
                                  mTuning.minDistance);
    if (target <= current)
        return target; // in, immediately: the wall is already there
    return std::min(target, current + std::max(mTuning.pushOutSpeed, 0.0f) * dt);
}

void ThirdPersonCameraRig::present(Renderer& r, const CameraPose& pose,
                                   float dt)
{
    const Solution solution = solve(pose, dt);

    // --- spring arm ------------------------------------------------------
    // One ray from the pivot back along the boom. A sphere cast would hug
    // corners better, but a ray plus a radius of slack is what this world's
    // geometry needs and it costs one query a frame.
    float blocked = solution.distance;
    if (mPhysics) {
        const glm::vec3 back = -directionFrom(solution.yaw, solution.pitch);
        const float probe = solution.distance + mTuning.collisionRadius;
        RayHit hit;
        if (mPhysics->rayCast(solution.pivot, back, probe, hit, mCollisionMask))
            blocked = hit.fraction * probe - mTuning.collisionRadius;
    }
    mBoomLength = boomLength(mBoomLength, solution.distance, blocked, dt);

    r.setPosition(mCharacter, pose.focus);
    r.setOrientation(mCharacter,
                     glm::angleAxis(pose.facingYaw, glm::vec3(0, 1, 0)));

    r.setPosition(mPivot, solution.pivot);
    const glm::vec3 shake = glm::radians(mShake.rotationDegrees);
    r.setOrientation(mBoomNode,
                     glm::angleAxis(solution.yaw, glm::vec3(0, 1, 0)) *
                         glm::angleAxis(solution.pitch, glm::vec3(1, 0, 0)));
    // Shake rides the eye, past the boom, so it can never feed back into the
    // spring arm: a camera that shook itself into a wall would then be pushed
    // by the wall, and the two would argue for as long as the shake lasted.
    r.setPosition(mEye, glm::vec3(0.0f, 0.0f, mBoomLength) + mShake.offset);
    r.setOrientation(mEye,
                     glm::angleAxis(shake.x, glm::vec3(1, 0, 0)) *
                         glm::angleAxis(pose.rollRadians + shake.z,
                                        glm::vec3(0, 0, 1)));

    mEyeWorld = solution.pivot -
                directionFrom(solution.yaw, solution.pitch) * mBoomLength;
}

} // namespace eng
