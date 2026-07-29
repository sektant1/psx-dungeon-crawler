#include "PhysicsSync.h"
#include "GameCollision.h"
#include "RuntimeComponents.h"

#include "GameComponents.h"

#include <eng/Physics.h>
#include <eng/ecs/Components.h>

#include <cstdlib>
#include <iostream>

using namespace game;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "PhysicsSyncTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    eng::Physics physics;
    physics.init(game::layer::physicsSetup());

    entt::registry reg;
    PhysicsSync sync(reg, physics);

    entt::entity wall = reg.create();
    eng::ecs::Transform t;
    t.position = glm::vec3(2, 0, 0);
    reg.emplace<eng::ecs::Transform>(wall, t);
    reg.emplace<Collider>(wall, Collider{eng::ShapeKind::Box, glm::vec3(1, 2, 1),
                                         game::layer::Static});

    const int before = physics.bodyCount();
    sync.sync();
    require(physics.bodyCount() == before + 1, "sync creates one body for the collider");
    require(reg.all_of<BodyRef>(wall), "collider entity gets a BodyRef");
    require(reg.get<BodyRef>(wall).handle.valid(), "BodyRef handle is valid");

    sync.sync();
    require(physics.bodyCount() == before + 1, "re-sync does not duplicate bodies");

    entt::entity trig = reg.create();
    reg.emplace<eng::ecs::Transform>(trig, eng::ecs::Transform{});
    reg.emplace<Trigger>(trig, Trigger{eng::ShapeKind::Box, glm::vec3(1), "door"});
    sync.sync();
    require(reg.all_of<BodyRef>(trig), "trigger entity gets a BodyRef");
    require(physics.bodyCount() == before + 2, "trigger adds one sensor body");

    reg.destroy(wall);
    sync.sync();
    require(physics.bodyCount() == before + 1, "destroyed entity's body is removed");

    physics.shutdown();
    std::cout << "PhysicsSyncTests OK\n";
    return 0;
}
