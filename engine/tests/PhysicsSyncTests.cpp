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
// A minimal layer table. The engine has no layer taxonomy of its own, so the
// test supplies one exactly as an application would.
constexpr eng::CollisionLayer kStatic = 0;
constexpr eng::CollisionLayer kTrigger = 1;
// Dynamic bodies need a layer the broadphase treats as moving; a prop on the
// static layer is the mistake this third entry exists to keep out of the test.
constexpr eng::CollisionLayer kProp = 2;

eng::PhysicsSetup setup()
{
    eng::PhysicsSetup s;
    s.layers.resize(3);
    s.layers[kStatic] = {"static", /*moving=*/false, {0.5f, 0.5f, 0.5f}};
    s.layers[kTrigger] = {"trigger", false, {1.0f, 0.4f, 1.0f}};
    s.layers[kProp] = {"prop", /*moving=*/true, {0.9f, 0.7f, 0.3f}};
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

    // --- an edited RigidBody rebuilds the body -----------------------------
    // Mass, friction and the rest are baked in at creation and Jolt has no
    // setter for them, so without this an inspector edit silently does nothing
    // until the level is reloaded.
    {
        entt::entity crate = reg.create();
        reg.emplace<Transform>(crate, Transform{});
        reg.emplace<Collider>(crate, Collider{eng::ShapeKind::Box,
                                              glm::vec3(0.5f), kProp});
        reg.emplace<RigidBody>(crate);
        sync.sync();
        require(reg.all_of<BodyRef>(crate), "dynamic prop gets a body");
        const eng::BodyHandle first = reg.get<BodyRef>(crate).handle;

        sync.sync();
        require(reg.get<BodyRef>(crate).handle == first,
                "an unchanged RigidBody keeps its body");

        // Asserted on behaviour, not on the handle: Physics recycles freed body
        // slots, so a remove-and-create inside one sync hands back the id it
        // just released. A handle comparison would therefore pass whether the
        // rebuild happened or not, which is the worst kind of green test.
        //
        // The same impulse on a heavier body moves it less. That is exactly the
        // thing the rebuild exists for -- Jolt bakes mass in at creation and has
        // no setter, so without it an inspector edit does nothing until the
        // level is reloaded.
        // alpha 1 reads where the body *is*; the default 0 reads where it was
        // before the step, which is right for rendering and useless here.
        physics.setInterpolationAlpha(1.0f);
        const auto driftUnderImpulse = [&](entt::entity e) {
            const eng::BodyHandle body = reg.get<BodyRef>(e).handle;
            const glm::vec3 from = reg.get<Transform>(e).position;
            physics.applyImpulse(body, glm::vec3(10.0f, 0.0f, 0.0f), from);
            for (int step = 0; step < 5; ++step)
                physics.update(1.0f / 60.0f);
            sync.readback();
            return reg.get<Transform>(e).position.x - from.x;
        };
        const float light = driftUnderImpulse(crate);
        require(light > 0.0f, "the impulse moved the light body");

        reg.get<RigidBody>(crate).mass = 12.0f;
        sync.sync();
        require(reg.all_of<BodyRef>(crate), "the rebuilt body is still there");
        const float heavy = driftUnderImpulse(crate);
        require(heavy < light * 0.5f,
                "an edited mass reaches the simulation: the same impulse moves "
                "a twelve-times-heavier body markedly less");

        reg.destroy(crate);
        sync.sync();
    }

    // --- a simulated pose flows back onto the Transform --------------------
    // The direction the registry does NOT own. Without readback a dynamic body
    // falls in Jolt and draws where it was authored, forever.
    {
        entt::entity falling = reg.create();
        Transform start;
        start.position = glm::vec3(0.0f, 20.0f, 0.0f);
        reg.emplace<Transform>(falling, start);
        reg.emplace<Collider>(falling, Collider{eng::ShapeKind::Sphere,
                                                glm::vec3(0.5f), kProp});
        reg.emplace<RigidBody>(falling);
        sync.sync();
        for (int i = 0; i < 30; ++i)
            physics.update(1.0f / 60.0f);
        sync.readback();
        require(reg.get<Transform>(falling).position.y < 19.9f,
                "the falling body's pose lands on its Transform");
        require(reg.all_of<Dirty>(falling),
                "and dirties it, so the renderer is told");

        // A body gameplay steers must not have its command overwritten by the
        // result of the last one.
        reg.emplace<KinematicControl>(falling);
        const glm::vec3 commanded(3.0f, 3.0f, 3.0f);
        reg.get<Transform>(falling).position = commanded;
        sync.readback();
        require(reg.get<Transform>(falling).position == commanded,
                "a kinematic-controlled body is not read back");
        reg.destroy(falling);
        sync.sync();
    }
    // Explicit teardown must not leak bodies across a map reload.
    sync.clear();
    require(physics.bodyCount() == before, "clear removes every tracked body");

    physics.shutdown();
    std::cout << "PhysicsSyncTests OK\n";
    return 0;
}
