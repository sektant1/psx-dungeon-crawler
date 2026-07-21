#pragma once
#include <eng/Handles.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace eng { class Physics; class Renderer; }

// Topple dummy: stands kinematic while alive; knocked to dynamic on kill.
// Proves the corpse/knockback path without a full ragdoll system.
class Dummy {
public:
    void init(eng::Physics&, eng::Renderer&, glm::vec3 feetPos);
    void kill(eng::Physics&, glm::vec3 impulse, glm::vec3 atPoint); // topple
    bool alive() const { return mAlive; }
    eng::BodyHandle body() const { return mBody; }
    void syncRender(eng::Physics&, eng::Renderer&);
    void clear(eng::Physics&, eng::Renderer&);

private:
    eng::BodyHandle mBody{};
    eng::NodeHandle mNode{};
    glm::vec3 mRenderOffset{0.0f};
    bool mAlive = true;
    bool mInitialised = false;
};
