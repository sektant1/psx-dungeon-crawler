#include "script/ScriptChunkCache.h"
#include "script/ScriptError.h"

#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/script/ScriptConfig.h>
#include <eng/script/ScriptHost.h>

#include <sol/sol.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace eng::script;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ScriptErrorTests: " << m << '\n'; std::exit(1); }
}

static std::string writeScript(const std::string& name, const std::string& body)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "eng_script_error_tests";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / name;
    std::ofstream(file) << body;
    return file.string();
}

// Attaches one script with no props.
static void attach(eng::ecs::World& w, entt::entity e, const std::string& path)
{
    w.registry().get_or_emplace<eng::ecs::Scripts>(e).items.push_back(
        {path, {}, true});
}

int main()
{
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                       sol::lib::table, sol::lib::debug);
    installTracebackHandler(lua);

    // --- a valid chunk returning a table becomes a class -------------------
    {
        const std::string file = writeScript("good.lua",
                                             "local M = {}\n"
                                             "function M:update(dt) end\n"
                                             "return M\n");
        ScriptChunkCache cache(lua);
        sol::table* cls = cache.classFor(file);
        require(cls != nullptr, "a chunk returning a table loads");
        require((*cls)["update"].valid(), "its methods are reachable");
        require(cache.classFor(file) == cls,
                "a second request is served from the cache, not re-run");
    }

    // --- a syntax error is reported at load, and yields no class -----------
    {
        const std::string file = writeScript("broken.lua",
                                             "function M:update( end\n");
        ScriptChunkCache cache(lua);
        require(cache.classFor(file) == nullptr,
                "a chunk that does not parse produces no class");
        require(cache.classFor(file) == nullptr,
                "and asking twice does not crash on the remembered failure");
    }

    // --- a chunk that does not return a table is a load error --------------
    {
        const std::string file = writeScript("noreturn.lua", "local M = {}\n");
        ScriptChunkCache cache(lua);
        require(cache.classFor(file) == nullptr,
                "a script must return its class table");
    }

    // --- a chunk that raises while loading is caught, not propagated -------
    {
        const std::string file = writeScript("throws.lua",
                                             "error('boom at load')\n"
                                             "return {}\n");
        ScriptChunkCache cache(lua);
        require(cache.classFor(file) == nullptr,
                "an error raised while the chunk runs is a load failure");
    }

    // --- a file that does not exist ----------------------------------------
    {
        ScriptChunkCache cache(lua);
        require(cache.classFor("/nowhere/absolutely_not.lua") == nullptr,
                "an unreadable path fails cleanly rather than throwing");
    }

    // --- the traceback handler produces a multi-frame trace ----------------
    {
        const std::string file =
            writeScript("deep.lua",
                        "local M = {}\n"
                        "local function inner() error('deep') end\n"
                        "local function outer() inner() end\n"
                        "function M:update(dt) outer() end\n"
                        "return M\n");
        ScriptChunkCache cache(lua);
        sol::table* cls = cache.classFor(file);
        require(cls != nullptr, "the deep script loads");

        sol::protected_function fn((*cls)["update"].get<sol::function>(),
                                   tracebackHandler(lua));
        const sol::protected_function_result r = fn(*cls, 0.016f);
        require(!r.valid(), "the call fails");
        const std::string msg = r.get<std::string>();
        require(msg.find("stack traceback") != std::string::npos,
                "the failure carries a traceback, not just the top frame");
        require(msg.find("deep.lua") != std::string::npos,
                "and the traceback names the chunk by its path -- the '@' "
                "prefix on the chunk name is what buys this");
        require(msg.find("[string \"") == std::string::npos,
                "not as a quoted source blob");
    }

    // --- reload replaces the class, and a broken reload keeps the old one --
    {
        const std::string file = writeScript("swap.lua",
                                             "local M = {}\n"
                                             "M.tag = 'first'\n"
                                             "return M\n");
        ScriptChunkCache cache(lua);
        sol::table* cls = cache.classFor(file);
        require(cls != nullptr && (*cls)["tag"].get<std::string>() == "first",
                "loaded the first version");

        writeScript("swap.lua", "local M = {}\nM.tag = 'second'\nreturn M\n");
        require(cache.reload(file), "reload succeeds");
        require(cache.classFor(file)->get<std::string>("tag") == "second",
                "and the class table is the new one");

        writeScript("swap.lua", "local M = {\n"); // half-typed save
        require(!cache.reload(file), "a broken reload reports failure");
        require(cache.classFor(file)->get<std::string>("tag") == "second",
                "and keeps the previous class live -- a half-typed save must "
                "not kill a running level");
    }

    // --- a runtime error quarantines exactly one instance ------------------
    {
        eng::ecs::World world;
        ScriptHost host(world, ScriptConfig{}, eng::ecs::engineRegistry());
        const std::string path = writeScript("boom.lua",
                                             "local M = {}\n"
                                             "function M:update(dt)\n"
                                             "  ticks = (ticks or 0) + 1\n"
                                             "  error('kaboom')\n"
                                             "end\n"
                                             "return M\n");
        const entt::entity a = world.create("a");
        const entt::entity b = world.create("b");
        attach(world, a, path);
        attach(world, b, path);

        host.tick(0.016f);
        require(host.luaGlobalNumber("ticks") == 2.0,
                "both instances ran and both failed");
        require(host.isQuarantined(a, path) && host.isQuarantined(b, path),
                "each failing instance is quarantined on its own");

        host.tick(0.016f);
        require(host.luaGlobalNumber("ticks") == 2.0,
                "a quarantined instance does not run again -- no per-frame "
                "spam of the same traceback");

        require(host.revive() == 2, "revive reports how many it restored");
        host.tick(0.016f);
        require(host.luaGlobalNumber("ticks") == 4.0, "and they run again");
    }

    // --- one broken instance does not stop its siblings --------------------
    {
        eng::ecs::World world;
        ScriptHost host(world, ScriptConfig{}, eng::ecs::engineRegistry());
        const std::string bad = writeScript("bad_sibling.lua",
                                            "local M = {}\n"
                                            "function M:update(dt) error('no') end\n"
                                            "return M\n");
        const std::string good =
            writeScript("good_sibling.lua",
                        "local M = {}\n"
                        "function M:update(dt) fine = (fine or 0) + 1 end\n"
                        "return M\n");
        const entt::entity e = world.create("mixed");
        auto& s = world.registry().get_or_emplace<eng::ecs::Scripts>(e);
        s.items.push_back({bad, {}, true});
        s.items.push_back({good, {}, true});

        host.tick(0.016f);
        host.tick(0.016f);
        require(host.luaGlobalNumber("fine") == 2.0,
                "the healthy script keeps ticking after its neighbour died");
    }

    // --- a failing start does not retry every frame ------------------------
    {
        eng::ecs::World world;
        ScriptHost host(world, ScriptConfig{}, eng::ecs::engineRegistry());
        const std::string path =
            writeScript("badstart.lua",
                        "local M = {}\n"
                        "function M:start() starts = (starts or 0) + 1\n"
                        "  error('bad start') end\n"
                        "return M\n");
        attach(world, world.create("s"), path);
        host.tick(0.016f);
        host.tick(0.016f);
        require(host.luaGlobalNumber("starts") == 1.0,
                "start is attempted once, even when it throws");
    }

    // --- a logical path resolves through the asset root --------------------
    //
    // Regression. Scripts are named by logical path in a scene
    // ("scripts/door.lua"), which is what makes a map portable, and the cache
    // opened that string directly -- so every scripted scene failed with
    // "cannot open the file" from any working directory but one. The unit
    // tests missed it because they all pass absolute paths.
    {
        const std::string absolute =
            writeScript("resolvable.lua", "local M = {}\nreturn M\n");

        // The path as given wins when it exists, which is what lets the tests
        // above pass absolute paths at all.
        require(resolveScriptPath(absolute) == absolute,
                "an existing path is used as-is");

        // And an unresolvable logical path comes back unchanged rather than
        // empty, so the failure is reported against the name the author wrote.
        require(resolveScriptPath("scripts/definitely_not_here.lua") ==
                    "scripts/definitely_not_here.lua",
                "an unresolved logical path is returned unchanged, so the error "
                "names what the author actually typed");
    }

    std::cout << "ScriptErrorTests: ok\n";
    return 0;
}
