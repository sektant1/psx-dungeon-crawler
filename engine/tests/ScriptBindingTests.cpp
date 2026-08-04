#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Dirty.h>
#include <eng/ecs/components/Name.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/ecs/components/Transform.h>
#include <eng/ecs/components/WorldTransform.h>
#include <eng/script/ScriptConfig.h>
#include <eng/script/ScriptHost.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace eng;
using namespace eng::ecs;
using namespace eng::script;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ScriptBindingTests: " << m << '\n'; std::exit(1); }
}

static std::filesystem::path gDir;

static std::string writeScript(const std::string& name, const std::string& body)
{
    std::filesystem::create_directories(gDir);
    const std::filesystem::path file = gDir / name;
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

static entt::entity scripted(World& w, const std::string& name,
                             const std::string& path)
{
    const entt::entity e = w.create(name);
    w.registry().get_or_emplace<Scripts>(e).items.push_back({path, {}, true});
    return e;
}

int main()
{
    gDir = std::filesystem::temp_directory_path() / "eng_script_binding_tests";
    std::filesystem::remove_all(gDir);

    // --- vec3 arithmetic ---------------------------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "vec.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  local a = vec3(1, 2, 3)\n"
            "  local b = vec3(0, 1, 0)\n"
            "  sum_y = (a + b).y\n"
            "  diff_x = (a - b).x\n"
            "  scaled = (a * 2).z\n"
            "  len = vec3(3, 4, 0):length()\n"
            "  norm = vec3(0, 5, 0):normalized().y\n"
            "  zero_norm = vec3(0, 0, 0):normalized().x\n"
            "  dotted = a:dot(b)\n"
            "  crossed = vec3(1, 0, 0):cross(vec3(0, 1, 0)).z\n"
            "end\n"
            "return M\n");
        scripted(world, "v", path);
        host.tick(0.016f);
        require(host.luaGlobalNumber("sum_y") == 3.0, "vec3 addition");
        require(host.luaGlobalNumber("diff_x") == 1.0, "vec3 subtraction");
        require(host.luaGlobalNumber("scaled") == 6.0, "vec3 scalar multiply");
        require(host.luaGlobalNumber("len") == 5.0, "vec3 length");
        require(host.luaGlobalNumber("norm") == 1.0, "vec3 normalized");
        require(host.luaGlobalNumber("zero_norm") == 0.0,
                "normalising a zero vector yields zero, not NaN -- a NaN here "
                "propagates into a transform and puts the entity nowhere");
        require(host.luaGlobalNumber("dotted") == 2.0, "vec3 dot");
        require(host.luaGlobalNumber("crossed") == 1.0, "vec3 cross");
    }

    // --- self.entity reads and writes the local Transform ------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "move.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  read_y = self.entity.position.y\n"
            "  self.entity.position = vec3(1, 5, 2)\n"
            "  who = self.entity.name\n"
            "  ok = self.entity.valid\n"
            "end\n"
            "return M\n");
        const entt::entity e = scripted(world, "mover", path);
        world.setLocalTransform(e, Transform{glm::vec3(0.0f, 7.0f, 0.0f)});
        world.updateWorldTransforms(); // clears Dirty, so the next write is ours
        host.tick(0.016f);

        require(host.luaGlobalNumber("read_y") == 7.0,
                "position reads the authored local transform");
        require(host.luaGlobalString("who") == "mover", "name reads Name");
        require(host.luaGlobalBool("ok"), "a live entity is valid");
        require(world.registry().get<Transform>(e).position.y == 5.0f,
                "writing position writes the local Transform");
        require(world.registry().all_of<Dirty>(e),
                "and marks the subtree dirty -- a write that bypassed "
                "setLocalTransform would draw at the old pose until something "
                "unrelated happened to move the entity");
    }

    // --- rotation is Euler degrees, and sets the right orientation ---------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "pose.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  self.entity.rotation = vec3(0, 120, 0)\n"
            "  self.entity.scale = vec3(2, 2, 2)\n"
            "  read_back = self.entity.rotation.y\n"
            "end\n"
            "return M\n");
        const entt::entity e = scripted(world, "poser", path);
        host.tick(0.016f);

        // The contract is the ORIENTATION, not the triple. Euler angles are
        // not unique: glm reads 120 degrees of yaw back as (180, 60, 180),
        // which is the same rotation spelled differently. Asserting the
        // quaternion is asserting what the entity actually does; asserting the
        // triple would be asserting a glm implementation detail.
        const glm::quat expected(glm::radians(glm::vec3(0.0f, 120.0f, 0.0f)));
        const glm::quat got = world.registry().get<Transform>(e).rotation;
        require(std::abs(glm::dot(expected, got)) > 0.9999f,
                "setting rotation in degrees produces that orientation");
        require(world.registry().get<Transform>(e).scale.x == 2.0f,
                "scale writes through");

        // And the round trip is exact where Euler angles are unambiguous, which
        // is the case a script author actually hits.
        World w2;
        ScriptHost h2(w2, ScriptConfig{}, engineRegistry());
        scripted(w2, "poser2", writeScript("pose45.lua",
                                           "local M = {}\n"
                                           "function M:start()\n"
                                           "  self.entity.rotation = vec3(0, 45, 0)\n"
                                           "  read_back = self.entity.rotation.y\n"
                                           "end\n"
                                           "return M\n"));
        h2.tick(0.016f);
        require(std::abs(h2.luaGlobalNumber("read_back") - 45.0) < 0.01,
                "an unambiguous angle round-trips exactly");
    }

    // --- world_position is derived and read-only ---------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "wp.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  wp = self.entity.world_position.y\n"
            "  local ok = pcall(function()\n"
            "    self.entity.world_position = vec3(0, 0, 0)\n"
            "  end)\n"
            "  refused = not ok\n"
            "end\n"
            "return M\n");
        const entt::entity parent = world.create("rig");
        world.setLocalTransform(parent, Transform{glm::vec3(0.0f, 10.0f, 0.0f)});
        const entt::entity e = scripted(world, "child", path);
        world.setParent(e, parent);
        world.setLocalTransform(e, Transform{glm::vec3(0.0f, 2.0f, 0.0f)});
        world.updateWorldTransforms();
        host.tick(0.016f);

        require(std::abs(host.luaGlobalNumber("wp") - 12.0) < 1e-4,
                "world_position is the composed pose, not the local one");
        require(host.luaGlobalBool("refused"),
                "assigning it is an error -- WorldTransform is derived, and a "
                "silent write would be overwritten on the next resolve");
    }

    // --- a destroyed entity reports invalid rather than crashing -----------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string hold = writeScript(
            "hold.lua",
            "local M = {}\n"
            "function M:start() held = self.entity end\n"
            "return M\n");
        const entt::entity e = scripted(world, "temp", hold);
        host.tick(0.016f);
        world.destroyHierarchy(e);

        const std::string probe = writeScript(
            "probe.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  stale_valid = held.valid\n"
            "  stale_name = held.name\n"
            "  stale_pos = held.position.y\n"
            "end\n"
            "return M\n");
        scripted(world, "prober", probe);
        host.tick(0.016f);
        require(!host.luaGlobalBool("stale_valid"),
                "a handle to a destroyed entity reports invalid");
        require(host.luaGlobalString("stale_name").empty(),
                "and reading through it yields a default, not a crash");
        require(host.luaGlobalNumber("stale_pos") == 0.0,
                "including its transform");
    }

    std::cout << "ScriptBindingTests: ok\n";
    return 0;
}
