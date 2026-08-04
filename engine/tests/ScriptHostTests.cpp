#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Scripts.h>
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
    if (!c) { std::cerr << "ScriptHostTests: " << m << '\n'; std::exit(1); }
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

// Attaches one script with no props.
static void attach(World& w, entt::entity e, const std::string& path,
                   bool enabled = true)
{
    w.registry().get_or_emplace<Scripts>(e).items.push_back({path, {}, enabled});
}

int main()
{
    gDir = std::filesystem::temp_directory_path() / "eng_script_host_tests";
    std::filesystem::remove_all(gDir);

    // A script that records its callbacks into Lua globals, so the test
    // observes behaviour through the VM rather than through host internals.
    const std::string counter =
        "local M = {}\n"
        "function M:start() calls = (calls or 0) + 1; started = true end\n"
        "function M:update(dt) ticks = (ticks or 0) + 1; last_dt = dt end\n"
        "return M\n";

    // --- start runs once, on the tick AFTER attach -------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript("counter.lua", counter);
        attach(world, world.create("thing"), path);

        require(!host.luaGlobalBool("started"),
                "start has not run merely because the component exists");
        host.tick(0.016f);
        require(host.luaGlobalBool("started"), "start runs on the first tick");
        require(host.luaGlobalNumber("calls") == 1.0, "start ran once");
        require(host.luaGlobalNumber("ticks") == 1.0,
                "update runs on the same tick, after start");

        host.tick(0.032f);
        require(host.luaGlobalNumber("calls") == 1.0, "start does not run again");
        require(host.luaGlobalNumber("ticks") == 2.0, "update runs every tick");
        require(host.luaGlobalNumber("last_dt") > 0.03,
                "update receives the frame's dt");
    }

    // --- self is per entity; the class is shared ---------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "perentity.lua",
            "local M = {}\n"
            "function M:start() self_count = (self_count or 0) + 1\n"
            "  self.mine = 0 end\n"
            "function M:update(dt) self.mine = self.mine + 1\n"
            "  total = (total or 0) + self.mine end\n"
            "return M\n");
        attach(world, world.create("a"), path);
        attach(world, world.create("b"), path);

        host.tick(0.016f);
        require(host.luaGlobalNumber("self_count") == 2.0,
                "each entity gets its own instance");
        require(host.luaGlobalNumber("total") == 2.0,
                "and its own state -- 1 + 1, not 1 + 2 from a shared table");
        require(host.instanceCount() == 2, "two live instances");
    }

    // --- several scripts on one entity run in author order -----------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string first = writeScript(
            "first.lua",
            "local M = {}\n"
            "function M:update(dt) order = (order or '') .. 'A' end\n"
            "return M\n");
        const std::string second = writeScript(
            "second.lua",
            "local M = {}\n"
            "function M:update(dt) order = (order or '') .. 'B' end\n"
            "return M\n");
        const entt::entity e = world.create("both");
        attach(world, e, first);
        attach(world, e, second);
        host.tick(0.016f);
        require(host.luaGlobalString("order") == "AB",
                "author order is run order");
    }

    // --- a disabled script never instantiates ------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript("counter2.lua", counter);
        attach(world, world.create("off"), path, /*enabled=*/false);
        host.tick(0.016f);
        require(host.instanceCount() == 0, "a disabled script is not created");
        require(!host.luaGlobalBool("started"), "and never starts");
    }

    // --- a script defining only some callbacks is legal --------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "startonly.lua",
            "local M = {}\n"
            "function M:start() ok = true end\n"
            "return M\n");
        attach(world, world.create("s"), path);
        host.tick(0.016f);
        host.tick(0.016f);
        require(host.luaGlobalBool("ok"),
                "a script may define only the callbacks it needs");
    }

    // --- a script that fails to load leaves the entity alone ---------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        attach(world, world.create("bad"), writeScript("bad.lua", "return 5\n"));
        host.tick(0.016f);
        require(host.instanceCount() == 0,
                "a chunk returning a non-table creates no instance");
        host.tick(0.016f); // must not crash, and must not re-report forever
    }

    // --- an entity attached later is picked up on the next tick ------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript("late.lua", counter);
        host.tick(0.016f);
        require(host.instanceCount() == 0, "nothing to run yet");

        attach(world, world.create("latecomer"), path);
        host.tick(0.016f);
        require(host.instanceCount() == 1,
                "a script attached mid-level starts on the next tick, which is "
                "what makes a spawned entity's script run at all");
        require(host.luaGlobalBool("started"), "and its start ran");
    }

    std::cout << "ScriptHostTests: ok\n";
    return 0;
}
