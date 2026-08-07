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
    if (!c) { std::cerr << "ScriptReloadTests: " << m << '\n'; std::exit(1); }
}

static std::filesystem::path gDir;

static std::string write(const std::string& name, const std::string& body)
{
    std::filesystem::create_directories(gDir);
    const std::filesystem::path file = gDir / name;
    std::ofstream(file) << body;
    return file.string();
}

static void attach(World& w, entt::entity e, const std::string& path)
{
    w.registry().get_or_emplace<Scripts>(e).items.push_back({path, {}, true});
}

int main()
{
    gDir = std::filesystem::temp_directory_path() / "eng_script_reload_tests";
    std::filesystem::remove_all(gDir);

    // --- reload swaps behaviour and KEEPS instance state -------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path =
            write("v.lua", "local M = {}\n"
                           "function M:start() self.n = 100 end\n"
                           "function M:update(dt) result = self.n + 1 end\n"
                           "return M\n");
        attach(world, world.create("subject"), path);
        host.tick(0.016f);
        require(host.luaGlobalNumber("result") == 101.0, "original behaviour");

        write("v.lua", "local M = {}\n"
                       "function M:start() self.n = 999 end\n"
                       "function M:update(dt) result = self.n + 2 end\n"
                       "function M:on_reload() reloaded = true end\n"
                       "return M\n");
        require(host.reload(path), "reload succeeds");
        host.tick(0.016f);

        require(host.luaGlobalNumber("result") == 102.0,
                "the new update() runs -- 100 + 2, so the method swapped");
        require(host.luaGlobalNumber("result") != 1001.0,
                "and start() did NOT re-run: re-running it would wipe the state "
                "an iteration loop is trying to preserve");
        require(host.luaGlobalBool("reloaded"), "on_reload fired");
    }

    // --- a reload that does not parse keeps the previous class -------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path =
            write("keep.lua", "local M = {}\n"
                              "function M:update(dt) alive = true end\n"
                              "return M\n");
        attach(world, world.create("keeper"), path);
        host.tick(0.016f);
        require(host.luaGlobalBool("alive"), "loaded");

        write("keep.lua", "function M:update( end\n"); // half-typed save
        require(!host.reload(path), "a broken reload reports failure");

        host.luaSetGlobalNil("alive");
        host.tick(0.016f);
        require(host.luaGlobalBool("alive"),
                "and the running level keeps working on the previous version");
    }

    // --- a successful reload revives a quarantined instance ----------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path =
            write("fixme.lua", "local M = {}\n"
                               "function M:update(dt) error('broken') end\n"
                               "return M\n");
        const entt::entity e = world.create("patient");
        attach(world, e, path);
        host.tick(0.016f);
        require(host.isQuarantined(e, path), "it errored and was quarantined");

        write("fixme.lua", "local M = {}\n"
                           "function M:update(dt) healed = true end\n"
                           "return M\n");
        require(host.reload(path), "the fixed version loads");
        require(!host.isQuarantined(e, path),
                "a successful reload revives it -- fixing the file is exactly "
                "how an author expects to un-break a script");
        host.tick(0.016f);
        require(host.luaGlobalBool("healed"), "and it runs again");
    }

    // --- reloading everything -----------------------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string a =
            write("multi_a.lua", "local M = {}\n"
                                 "function M:update(dt) ra = 1 end\n"
                                 "return M\n");
        const std::string b =
            write("multi_b.lua", "local M = {}\n"
                                 "function M:update(dt) rb = 1 end\n"
                                 "return M\n");
        attach(world, world.create("a"), a);
        attach(world, world.create("b"), b);
        host.tick(0.016f);

        write("multi_a.lua", "local M = {}\n"
                             "function M:update(dt) ra = 2 end\n"
                             "return M\n");
        write("multi_b.lua", "local M = {}\n"
                             "function M:update(dt) rb = 2 end\n"
                             "return M\n");
        require(host.reload(), "an empty path reloads every loaded script");
        host.tick(0.016f);
        require(host.luaGlobalNumber("ra") == 2.0 &&
                    host.luaGlobalNumber("rb") == 2.0,
                "and both picked up their new versions");
    }

    // --- the console evaluates expressions and statements ------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        std::string out;
        require(host.executeConsole("1 + 1", out), "an expression evaluates");
        require(out == "2", "and prints its result");

        require(host.executeConsole("answer = 42", out), "a statement runs");
        require(host.luaGlobalNumber("answer") == 42.0, "and takes effect");

        require(!host.executeConsole("this is not lua", out),
                "a broken line reports failure rather than throwing");
        require(!out.empty(), "and says why");
    }

    std::cout << "ScriptReloadTests: ok\n";
    return 0;
}
