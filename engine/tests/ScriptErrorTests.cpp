#include "script/ScriptChunkCache.h"
#include "script/ScriptError.h"

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

    std::cout << "ScriptErrorTests: ok\n";
    return 0;
}
