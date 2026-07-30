#include "EditorCamera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/common.hpp>
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
