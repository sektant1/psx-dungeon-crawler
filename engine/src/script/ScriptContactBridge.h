#pragma once
#include <entt/entt.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace eng {
class Physics;
struct HitEvent;
}

namespace eng::ecs { class World; }

namespace eng::script {

// One contact, in the vocabulary a script speaks: entities, not body handles.
struct ScriptContact {
    entt::entity self = entt::null;
    entt::entity other = entt::null;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    float impulse = 0.0f;
    bool sensor = false; // self's Collider is a sensor -> on_trigger
};

// Turns Physics contacts into script contacts.
//
// Queues rather than dispatching inline. Physics already delivers contacts on
// the main thread -- it collects them on Jolt's job threads and flushes them
// itself -- so this is NOT for thread safety. It is so Lua never runs inside
// Physics::update(), where a script destroying the entity it just hit would be
// mutating the registry mid-step.
class ScriptContactBridge {
public:
    ScriptContactBridge(Physics& physics, ecs::World& world);
    ~ScriptContactBridge();
    ScriptContactBridge(const ScriptContactBridge&) = delete;
    ScriptContactBridge& operator=(const ScriptContactBridge&) = delete;

    // Everything queued since the last call, cleared.
    std::vector<ScriptContact> drain();

private:
    Physics& mPhysics;
    ecs::World& mWorld;
    uint32_t mToken = 0;
    std::vector<ScriptContact> mQueue;
};

} // namespace eng::script
