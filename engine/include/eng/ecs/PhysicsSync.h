#pragma once

#include <eng/Physics.h>

#include <entt/entt.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <utility>
#include <vector>

namespace eng { class Physics; }

namespace eng::ecs {

// Drives physics from the registry, the way SceneSync drives the renderer:
// allocates a body when an entity first has a Collider, and frees it when the
// entity is destroyed. The registry is the source of truth; the physics world
// is a view of it.
//
// What a collider *means* is not decided here -- layers and sensor-ness are
// data on the component, so the application owns its collision taxonomy (see
// its layer table and collision matrix) and the engine only materialises it.
class PhysicsSync {
public:
    PhysicsSync(entt::registry& reg, Physics& physics)
        : mReg(reg), mPhysics(physics) {}
    ~PhysicsSync();

    PhysicsSync(const PhysicsSync&) = delete;
    PhysicsSync& operator=(const PhysicsSync&) = delete;

    // Run once per frame, after gameplay mutated the registry.
    void sync();
    // Remove every materialised body. Used before wholesale registry reloads;
    // also called by the destructor.
    void clear();

private:
    struct Tracked {
        entt::entity entity{entt::null};
        BodyHandle body{};
        ShapeKind shape = ShapeKind::Box;
        glm::vec3 size{0.5f};
        CollisionLayer layer = 0;
        bool sensor = false;
        glm::vec3 position{0.0f};
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    entt::registry& mReg;
    Physics& mPhysics;
    std::vector<Tracked> mTracked;
};

} // namespace eng::ecs
