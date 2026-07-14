#include "FpsController.h"

#include <eng/Input.h>
#include <eng/Renderer.h>

#include <cmath>

namespace {
constexpr float kEyeHeight = 1.7f;
const float kMaxPitch = glm::radians(89.0f);
} // namespace

void FpsController::init(eng::Renderer& r, glm::vec3 startPos, float speed,
                         float sensitivity, glm::vec3 roomMin, glm::vec3 roomMax)
{
    mPos = startPos;
    mSpeed = speed;
    mSens = sensitivity;
    mMin = roomMin;
    mMax = roomMax;
    mBody = r.createNode(eng::kRootNode, mPos);
    mHead = r.createNode(mBody, {0.0f, kEyeHeight, 0.0f});
    r.attachCamera(mHead);
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
    if (glm::length(move) > 0.0f)
        mPos += glm::normalize(move) * mSpeed * dt;
    mPos = glm::clamp(mPos, mMin, mMax);

    r.setPosition(mBody, mPos);
    r.setOrientation(mBody, glm::angleAxis(mYaw, glm::vec3(0, 1, 0)));
    r.setOrientation(mHead, glm::angleAxis(mPitch, glm::vec3(1, 0, 0)));
}
