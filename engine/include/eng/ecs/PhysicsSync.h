#pragma once

#include <eng/Physics.h>
#include <eng/ecs/World.h>

#include <entt/entt.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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
// Owned by World (World::attachPhysics); construct one directly only in a test.
class PhysicsSync : public WorldReconciler {
public:
    PhysicsSync(entt::registry& reg, Physics& physics)
        : mReg(reg), mPhysics(physics) {}
    ~PhysicsSync() override;

    PhysicsSync(const PhysicsSync&) = delete;
    PhysicsSync& operator=(const PhysicsSync&) = delete;

    // Run once per frame, after gameplay mutated the registry.
    void sync() override;
    // Simulated poses -> Transform, before the renderer reads them. Without it
    // a dynamic body falls in Jolt and draws where it was authored: the
    // registry is the source of truth for everything except the one thing the
    // simulation owns, and this is where that exception is reconciled.
    void readback() override;
    // Remove every materialised body. Used before wholesale registry reloads;
    // also called by the destructor.
    void clear() override;

private:
    // What the body was built from. Everything Jolt bakes in at creation and
    // offers no setter for lives here, and a difference against the components
    // is a rebuild -- which is the only way "edit the mass in the inspector and
    // watch it fall differently" works at all.
    struct Tracked {
        entt::entity entity{entt::null};
        BodyHandle body{};
        ShapeKind shape = ShapeKind::Box;
        glm::vec3 size{0.5f};
        CollisionLayer layer = 0;
        bool sensor = false;
        bool dynamic = false;
        RigidBody params{};
        glm::vec3 position{0.0f};
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    // Whether two RigidBodys would produce the same body. Lives here rather
    // than as an operator== on the component so that adding a field to
    // RigidBody without deciding whether it forces a rebuild is a compile
    // error's worth of thought instead of a silent no-op.
    static bool sameBuild(const RigidBody& a, const RigidBody& b);

    entt::registry& mReg;
    Physics& mPhysics;
    std::vector<Tracked> mTracked;
};

} // namespace eng::ecs
