#include "EditorCamera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <algorithm>
#include <cmath>

void EditorCamera::orbit(float dYawRad, float dPitchRad) {
    mYaw += dYawRad;
    mPitch = std::clamp(mPitch + dPitchRad, -1.5f, 1.5f); // avoid gimbal flip
}
void EditorCamera::dolly(float delta) {
    mDistance = std::clamp(mDistance - delta * 0.5f, 0.5f, 200.0f);
}
void EditorCamera::pan(glm::vec3 worldDelta) { mTarget += worldDelta; }
void EditorCamera::frame(glm::vec3 target, float distance) {
    mTarget = target;
    mDistance = std::clamp(distance, 0.5f, 200.0f);
}

glm::vec3 EditorCamera::eye() const {
    const float cp = std::cos(mPitch), sp = std::sin(mPitch);
    const float cy = std::cos(mYaw), sy = std::sin(mYaw);
    const glm::vec3 dir(cp * sy, sp, cp * cy); // unit direction from target to eye
    return mTarget + dir * mDistance;
}
glm::quat EditorCamera::orientation() const {
    const glm::mat4 view = glm::lookAt(eye(), mTarget, glm::vec3(0, 1, 0));
    // orientation = inverse of the view rotation
    return glm::quat_cast(glm::transpose(glm::mat3(view)));
}

void EditorCamera::setYawPitch(float yawRad, float pitchRad)
{
    mYaw = yawRad;
    mPitch = glm::clamp(pitchRad, glm::radians(-89.9f), glm::radians(89.9f));
}

void EditorCamera::addYawPitch(float dYawRad, float dPitchRad)
{
    setYawPitch(mYaw + dYawRad, mPitch + dPitchRad);
}

glm::quat EditorCamera::flyOrientation() const
{
    return glm::angleAxis(mYaw, glm::vec3(0, 1, 0)) *
           glm::angleAxis(mPitch, glm::vec3(1, 0, 0));
}

void EditorCamera::moveLocal(glm::vec3 localDelta)
{
    mFlyPos += flyOrientation() * localDelta;
}

namespace {
// Keeps yaw in (-pi, pi] so it stays readable in the debug UI and so repeated
// mouse motion in one direction cannot accumulate into a large float where the
// trig loses precision. Wrapping is invisible to the resulting orientation.
float wrapAngle(float radians)
{
    constexpr float kTwoPi = 6.28318530717958647692f;
    radians = std::fmod(radians + glm::pi<float>(), kTwoPi);
    if (radians <= 0.0f) radians += kTwoPi;
    return radians - glm::pi<float>();
}
// A human neck cannot quite look straight up or down, and a full +/-90 deg
// pitch would make the yaw-only basis degenerate.
constexpr float kMaxPitchDeg = 85.0f;
} // namespace

void EditorCamera::enterWalk(glm::vec3 floorPosition, float yawDegrees)
{
    // Snapshot first: re-entering walk while already walking must not overwrite
    // the orbit/fly state we still owe the author on leaveWalk().
    if (!mWalking) {
        mSavedTarget = mTarget;
        mSavedDistance = mDistance;
        mSavedYaw = mYaw;
        mSavedPitch = mPitch;
        mSavedFlyPos = mFlyPos;
        mWalking = true;
    }
    mWalkFloor = floorPosition;
    mWalkYaw = wrapAngle(glm::radians(yawDegrees));
    mWalkPitch = 0.0f; // level horizon: the player spawns looking straight ahead
}

void EditorCamera::leaveWalk()
{
    if (!mWalking) return;
    mTarget = mSavedTarget;
    mDistance = mSavedDistance;
    mYaw = mSavedYaw;
    mPitch = mSavedPitch;
    mFlyPos = mSavedFlyPos;
    mWalking = false;
}

void EditorCamera::setWalkFloorPosition(glm::vec3 floorPosition)
{
    mWalkFloor = floorPosition;
}

void EditorCamera::walkLook(float dYawRad, float dPitchRad)
{
    mWalkYaw = wrapAngle(mWalkYaw + dYawRad);
    mWalkPitch = glm::clamp(mWalkPitch + dPitchRad,
                            glm::radians(-kMaxPitchDeg),
                            glm::radians(kMaxPitchDeg));
}

void EditorCamera::walkMove(glm::vec3 localDelta)
{
    // Movement uses the yaw-only basis, not the full orientation: looking at
    // the ceiling must not fly the preview upward, and the eye height is a
    // constant offset from the floor position, so any vertical component of
    // the request is simply dropped.
    const glm::quat yawOnly = glm::angleAxis(mWalkYaw, glm::vec3(0, 1, 0));
    const glm::vec3 world = yawOnly * glm::vec3(localDelta.x, 0.0f, localDelta.z);
    mWalkFloor += glm::vec3(world.x, 0.0f, world.z);
}

glm::vec3 EditorCamera::walkEye() const
{
    return mWalkFloor + glm::vec3(0.0f, kPlayerEyeHeight, 0.0f);
}

glm::quat EditorCamera::walkOrientation() const
{
    // Yaw about world up then pitch about the local right axis. There is no
    // third term, so the preview can never roll.
    return glm::angleAxis(mWalkYaw, glm::vec3(0, 1, 0)) *
           glm::angleAxis(mWalkPitch, glm::vec3(1, 0, 0));
}
