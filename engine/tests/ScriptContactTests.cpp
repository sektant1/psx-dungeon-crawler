#include <eng/Physics.h>
#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Collider.h>
#include <eng/ecs/components/RigidBody.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/ecs/components/Transform.h>
#include <eng/script/ScriptConfig.h>
#include <eng/script/ScriptHost.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace eng;
using namespace eng::ecs;
using namespace eng::script;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ScriptContactTests: " << m << '\n'; std::exit(1); }
}

static std::string writeScript(const std::string& name, const std::string& body)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "eng_script_contact_tests";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / name;
    std::ofstream(file) << body;
    return file.string();
}

static const ComponentRegistry& engineRegistry()
{
    static ComponentRegistry reg = [] {
        ComponentRegistry r;
        registerEngineComponents(r);
        return r;
    }();
    return reg;
}

static void attach(World& w, entt::entity e, const std::string& path)
{
    w.registry().get_or_emplace<Scripts>(e).items.push_back({path, {}, true});
}

// One frame in the order the game runs it: fixed_update, physics, contacts,
// update, sync.
static void frame(ScriptHost& host, Physics& physics, World& world)
{
    host.fixedTick(1.0f / 60.0f);
    physics.update(1.0f / 60.0f);
    host.drainContacts();
    world.sync();
    host.tick(1.0f / 60.0f);
}

int main()
{
    // --- a solid contact calls on_collision --------------------------------
    {
        Physics physics;
        physics.init();
        World world;
        world.attachPhysics(physics);
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        host.bindPhysics(physics);

        const std::string path = writeScript(
            "hit.lua",
            "local M = {}\n"
            "function M:on_collision(other, hit)\n"
            "  hits = (hits or 0) + 1\n"
            "  other_name = other and other.name or ''\n"
            "  has_point = hit.point ~= nil\n"
            "end\n"
            "return M\n");

        const entt::entity floor = world.create("floor");
        world.setLocalTransform(floor, Transform{glm::vec3(0.0f, -0.5f, 0.0f)});
        world.registry().emplace<Collider>(
            floor, Collider{ShapeKind::Box, glm::vec3(10.0f, 0.5f, 10.0f)});

        const entt::entity box = world.create("box");
        world.setLocalTransform(box, Transform{glm::vec3(0.0f, 3.0f, 0.0f)});
        // Layer 1 is generic()'s "dynamic"; layer 0 is "static", and
        // generic() disables static-vs-static as dead work, so a falling
        // body left on the default layer never touches the floor.
        world.registry().emplace<Collider>(
            box, Collider{ShapeKind::Box, glm::vec3(0.5f), /*layer=*/1});
        world.registry().emplace<RigidBody>(box);
        attach(world, box, path);

        world.sync();
        for (int i = 0; i < 240 && host.luaGlobalNumber("hits") == 0.0; ++i)
            frame(host, physics, world);

        require(host.luaGlobalNumber("hits") > 0.0,
                "the box landed and its script heard about it");
        require(host.luaGlobalString("other_name") == "floor",
                "and was told which ENTITY it hit, not a body handle");
        require(host.luaGlobalBool("has_point"),
                "the hit carries its contact point");

        world.detachAll();
        physics.shutdown();
    }

    // --- a sensor collider calls on_trigger instead ------------------------
    {
        Physics physics;
        physics.init();
        World world;
        world.attachPhysics(physics);
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        host.bindPhysics(physics);

        const std::string path = writeScript(
            "volume.lua",
            "local M = {}\n"
            "function M:on_trigger(other) triggered = true end\n"
            "function M:on_collision(other, hit) collided = true end\n"
            "return M\n");

        Collider sensor{ShapeKind::Box, glm::vec3(2.0f)};
        sensor.sensor = true;
        const entt::entity volume = world.create("volume");
        world.setLocalTransform(volume, Transform{glm::vec3(0.0f, 0.0f, 0.0f)});
        world.registry().emplace<Collider>(volume, sensor);
        attach(world, volume, path);

        const entt::entity faller = world.create("faller");
        world.setLocalTransform(faller, Transform{glm::vec3(0.0f, 4.0f, 0.0f)});
        world.registry().emplace<Collider>(
            faller, Collider{ShapeKind::Box, glm::vec3(0.4f), /*layer=*/1});
        world.registry().emplace<RigidBody>(faller);

        world.sync();
        for (int i = 0; i < 300 && !host.luaGlobalBool("triggered"); ++i)
            frame(host, physics, world);

        require(host.luaGlobalBool("triggered"),
                "a sensor collider reports through on_trigger");
        require(!host.luaGlobalBool("collided"),
                "and NOT through on_collision -- the sensor flag is what lets a "
                "trigger volume and a wall be the same kind of object");

        world.detachAll();
        physics.shutdown();
    }

    // --- a host with no physics bound simply has no contacts ---------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        host.drainContacts(); // must be a harmless no-op
        host.tick(0.016f);
    }

    std::cout << "ScriptContactTests: ok\n";
    return 0;
}
