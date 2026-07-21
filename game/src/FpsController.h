#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <functional>

namespace eng {
class Input;
class Renderer;
} // namespace eng

// First-person controller: yaw on a body node, pitch on a head node
// (camera attached at eye height). Movement on the ground plane, clamped
// to a room AABB, or resolved by a caller-provided collider (setResolver).
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
    MoveResolver mResolve;
};
