#include <eng/ecs/PhysicsSync.h>

#include <eng/Physics.h>
#include <eng/ecs/Components.h>

#include <cstdlib>
#include <iostream>

using namespace eng::ecs;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "PhysicsSyncTests: " << m << '\n'; std::exit(1); }
}

namespace {
// A minimal two-layer world. The engine has no layer taxonomy of its own, so
// the test supplies one exactly as an application would.
constexpr eng::CollisionLayer kStatic = 0;
constexpr eng::CollisionLayer kTrigger = 1;

eng::PhysicsSetup setup()
{
    eng::PhysicsSetup s;
    s.layers.resize(2);
    s.layers[kStatic] = {"static", /*moving=*/false, {0.5f, 0.5f, 0.5f}};
    s.layers[kTrigger] = {"trigger", false, {1.0f, 0.4f, 1.0f}};
    s.collideAll();
    s.setPair(kStatic, kStatic, false);
    s.characterLayer = kStatic;
    return s;
}
} // namespace

int main()
{
    eng::Physics physics;
    physics.init(setup());

    entt::registry reg;
    PhysicsSync sync(reg, physics);

    entt::entity wall = reg.create();
    Transform t;
    t.position = glm::vec3(2, 0, 0);
    reg.emplace<Transform>(wall, t);
    WorldTransform world;
    world.matrix[3] = glm::vec4(5, 0, 0, 1);
    reg.emplace<WorldTransform>(wall, world);
    reg.emplace<Collider>(wall, Collider{eng::ShapeKind::Box, glm::vec3(1, 2, 1),
                                         kStatic});

    const int before = physics.bodyCount();
    sync.sync();
    require(physics.bodyCount() == before + 1, "sync creates one body for the collider");
    require(reg.all_of<BodyRef>(wall), "collider entity gets a BodyRef");
    require(reg.get<BodyRef>(wall).handle.valid(), "BodyRef handle is valid");
    {
        glm::vec3 position; glm::quat orientation;
        physics.getRenderTransform(reg.get<BodyRef>(wall).handle, position,
                                   orientation);
        require(position == glm::vec3(5, 0, 0),
                "body uses the renderer-facing world position when available");
    }

    sync.sync();
    require(physics.bodyCount() == before + 1, "re-sync does not duplicate bodies");

    // A trigger volume is just a sensor collider as far as the engine is
    // concerned; which layer it lives on is the application's decision.
    entt::entity trig = reg.create();
    reg.emplace<Transform>(trig, Transform{});
    reg.emplace<Collider>(trig, Collider{eng::ShapeKind::Box, glm::vec3(1),
                                         kTrigger, /*sensor=*/true});
    sync.sync();
    require(reg.all_of<BodyRef>(trig), "sensor collider entity gets a BodyRef");
    require(physics.bodyCount() == before + 2, "sensor collider adds one body");

    reg.remove<Collider>(trig);
    sync.sync();
    require(!reg.all_of<BodyRef>(trig),
            "removing Collider removes the materialised BodyRef");
    require(physics.bodyCount() == before + 1,
            "removing Collider destroys its physics body");

    entt::entity invalid = reg.create();
    reg.emplace<Transform>(invalid, Transform{});
    reg.emplace<Collider>(invalid,
                          Collider{eng::ShapeKind::Box, glm::vec3(1), 15});
    sync.sync();
    require(!reg.all_of<BodyRef>(invalid),
            "failed body creation remains retryable without an invalid BodyRef");

    reg.destroy(wall);
    sync.sync();
    require(physics.bodyCount() == before, "destroyed entity's body is removed");

    reg.emplace<Collider>(trig, Collider{eng::ShapeKind::Box, glm::vec3(1),
                                         kTrigger, /*sensor=*/true});
    sync.sync();
    require(physics.bodyCount() == before + 1,
            "re-added collider is materialised after removal");
    // Explicit teardown must not leak bodies across a map reload.
    sync.clear();
    require(physics.bodyCount() == before, "clear removes every tracked body");

    physics.shutdown();
    std::cout << "PhysicsSyncTests OK\n";
    return 0;
}
