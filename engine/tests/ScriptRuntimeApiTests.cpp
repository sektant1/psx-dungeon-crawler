// save.* and the RuntimeHooks bindings (game.*, camera.*).
//
// Headless, and deliberately without a runtime behind the hooks in the second
// half: an unset hook must bind a call that logs and does nothing, because a
// script written for the player should still load in a combat sim.

#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/script/ScriptHost.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace eng;
using namespace eng::script;

namespace fs = std::filesystem;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "ScriptRuntimeApiTests: " << m << '\n';
        std::exit(1);
    }
}

static fs::path dir()
{
    const fs::path d = fs::temp_directory_path() / "raven_runtime_api_tests";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

static std::string writeScript(const char* name, const char* body)
{
    const fs::path path = dir() / name;
    std::ofstream out(path, std::ios::trunc);
    out << body;
    return path.string();
}

static void attach(ecs::World& world, entt::entity e, const std::string& path)
{
    ecs::Scripts s;
    s.items.push_back(ecs::ScriptRef{path, {}, true});
    world.registry().emplace<ecs::Scripts>(e, std::move(s));
}

// Set, read back, persist, and survive a new state reading the same file.
static void testSaveRoundTrip()
{
    const fs::path file = dir() / "save.txt";
    std::error_code ec;
    fs::remove(file, ec);

    const std::string path = writeScript("save.lua", R"(
local T = {}
function T:start()
  save.set("checkpoint", 3)
  save.set("name", "ilsabet")
  save.set("door_open", true)
  -- Readable before it is committed: the in-memory copy is the authority.
  read_back = save.get("checkpoint", 0)
  missing = save.get("nope", 42)
  save.commit()
end
return T
)");

    {
        ecs::World world;
        ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
        host.bindSave(file.string());
        attach(world, world.create("thing"), path);
        host.tick(0.0f);
        require(host.luaGlobalNumber("read_back") == 3, "set then get");
        require(host.luaGlobalNumber("missing") == 42,
                "a missing key returns the default");
    }

    require(fs::is_regular_file(file, ec), "commit wrote the file");

    // A second host over the same file is what a relaunch is.
    const std::string reader = writeScript("load.lua", R"(
local T = {}
function T:start()
  checkpoint = save.get("checkpoint", 0)
  name = save.get("name", "")
  door = save.get("door_open", false)
  has_missing = save.has("nope")
end
return T
)");

    ecs::World world;
    ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
    host.bindSave(file.string());
    attach(world, world.create("thing"), reader);
    host.tick(0.0f);
    require(host.luaGlobalNumber("checkpoint") == 3, "numbers survive a reload");
    require(host.luaGlobalString("name") == "ilsabet", "so do strings");
    require(host.luaGlobalBool("door"), "and booleans");
    require(!host.luaGlobalBool("has_missing"), "has() is false for unset keys");
}

// Large numbers must survive a commit/load: the default ostream precision is
// six significant figures, which turned 1234567 into 1234570.
static void testSavePrecisionAndTypes()
{
    const fs::path file = dir() / "precision.txt";
    std::error_code ec;
    fs::remove(file, ec);

    const std::string writer = writeScript("precise.lua", R"(
local T = {}
function T:start()
  save.set("score", 1234567)
  save.set("when", 1723058399)
  save.set("label", "seven")
  save.commit()
end
return T
)");
    {
        ecs::World world;
        ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
        host.bindSave(file.string());
        attach(world, world.create("thing"), writer);
        host.tick(0.0f);
    }

    const std::string reader = writeScript("imprecise.lua", R"(
local T = {}
function T:start()
  score = save.get("score", 0)
  when = save.get("when", 0)
  -- A key stored as a string, asked for as a number: the documented answer is
  -- the default, not the string.
  mismatched = save.get("label", -1)
end
return T
)");
    ecs::World world;
    ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
    host.bindSave(file.string());
    attach(world, world.create("thing"), reader);
    host.tick(0.0f);

    require(host.luaGlobalNumber("score") == 1234567.0,
            "a seven-digit number round-trips exactly");
    require(host.luaGlobalNumber("when") == 1723058399.0,
            "and a ten-digit one");
    require(host.luaGlobalNumber("mismatched") == -1.0,
            "a key of the wrong type reads as the default");
}

static void testSaveClear()
{
    const fs::path file = dir() / "clear.txt";
    std::error_code ec;
    fs::remove(file, ec);

    const std::string path = writeScript("clear.lua", R"(
local T = {}
function T:start()
  save.set("gold", 100)
  save.commit()
  save.clear()
  after_clear = save.get("gold", -1)
end
return T
)");

    ecs::World world;
    ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
    host.bindSave(file.string());
    attach(world, world.create("thing"), path);
    host.tick(0.0f);
    require(host.luaGlobalNumber("after_clear") == -1,
            "clear drops everything, so a new game starts new");
}

// The hooks a runtime fills in.
static void testRuntimeHooks()
{
    const std::string path = writeScript("runtime.lua", R"(
local T = {}
function T:start()
  requested = "none"
  game.load_scene("scenes/level2.scn")
  t = game.time()
  scale_before = game.time_scale()
  game.set_time_scale(0.25)
  eye = camera.position()
  fwd = camera.forward()
end
return T
)");

    std::string requestedScene;
    float scale = 1.0f;

    ecs::World world;
    ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
    RuntimeHooks hooks;
    hooks.loadScene = [&](const std::string& s) { requestedScene = s; };
    hooks.elapsed = [] { return 12.5; };
    hooks.timeScale = [&] { return scale; };
    hooks.setTimeScale = [&](float s) { scale = s; };
    hooks.cameraPosition = [] { return glm::vec3(1.0f, 2.0f, 3.0f); };
    hooks.cameraForward = [] { return glm::vec3(0.0f, 0.0f, -1.0f); };
    host.bindRuntime(hooks);

    attach(world, world.create("thing"), path);
    host.tick(0.0f);

    require(requestedScene == "scenes/level2.scn",
            "load_scene reaches the runtime, by the authored name");
    require(host.luaGlobalNumber("t") == 12.5, "game.time is the game clock");
    require(host.luaGlobalNumber("scale_before") == 1.0,
            "the scale is read through the hook");
    require(scale == 0.25f, "and written through it");
}

// No runtime behind the hooks: the calls must exist and do nothing, rather
// than being nil.
static void testUnboundHooksAreSafe()
{
    const std::string path = writeScript("unbound.lua", R"(
local T = {}
function T:start()
  -- None of these has a runtime behind it. Each should log and continue.
  game.load_scene("scenes/whatever.scn")
  game.quit()
  game.set_time_scale(0.5)
  survived = true
  scale = game.time_scale()
  t = game.time()
end
return T
)");

    ecs::World world;
    ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
    host.bindRuntime(RuntimeHooks{}); // every field unset
    attach(world, world.create("thing"), path);
    host.tick(0.0f);

    require(host.luaGlobalBool("survived"),
            "an unset hook does not abort the script that called it");
    require(host.luaGlobalNumber("scale") == 1.0, "and reads a sane default");
    require(host.luaGlobalNumber("t") == 0.0, "likewise the clock");
}

int main()
{
    testSaveRoundTrip();
    testSavePrecisionAndTypes();
    testSaveClear();
    testRuntimeHooks();
    testUnboundHooksAreSafe();
    std::puts("ScriptRuntimeApiTests: ok");
    return 0;
}
