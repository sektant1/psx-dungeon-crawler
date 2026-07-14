#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

namespace eng {
class Input;
class Renderer;
} // namespace eng

// First-person controller: yaw on a body node, pitch on a head node
// (camera attached at eye height). Movement on the ground plane, clamped
// to a room AABB -- no collision system in the scaffold.
class FpsController
{
public:
    void init(eng::Renderer& r, glm::vec3 startPos, float speed,
              float sensitivity, glm::vec3 roomMin, glm::vec3 roomMax);
    void update(eng::Input& in, eng::Renderer& r, float dt);

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
};
