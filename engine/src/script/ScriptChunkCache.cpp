#include "script/ScriptChunkCache.h"

#include "script/ScriptError.h"

#include <fstream>
#include <sstream>

namespace eng::script {

std::optional<sol::table> ScriptChunkCache::load(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        reportScriptError(path, "load", {}, "cannot open the file");
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string source = ss.str();

    // The '@' prefix is what makes a traceback read "scripts/door.lua:12:"
    // instead of '[string "local M = {}..."]:12:'. Lua treats a chunk name
    // starting with @ as a filename; without it every frame quotes the source.
    const std::string chunkName = "@" + path;

    sol::load_result chunk = mLua.load(source, chunkName);
    if (!chunk.valid()) {
        const sol::error err = chunk;
        reportScriptError(path, "load", {}, err.what());
        return std::nullopt;
    }

    sol::protected_function fn(chunk.get<sol::function>(),
                               tracebackHandler(mLua));
    const sol::protected_function_result result = fn();
    if (!result.valid()) {
        const sol::error err = result;
        reportScriptError(path, "load", {}, err.what());
        return std::nullopt;
    }
    if (result.get_type() != sol::type::table) {
        reportScriptError(path, "load", {},
                          "the chunk must return its class table "
                          "(add 'return M' at the end)");
        return std::nullopt;
    }
    return result.get<sol::table>();
}

sol::table* ScriptChunkCache::classFor(const std::string& path)
{
    if (const auto it = mClasses.find(path); it != mClasses.end())
        return &it->second;
    if (mFailed.count(path) != 0)
        return nullptr;

    std::optional<sol::table> cls = load(path);
    if (!cls) {
        mFailed[path] = true;
        return nullptr;
    }
    return &mClasses.emplace(path, std::move(*cls)).first->second;
}

bool ScriptChunkCache::reload(const std::string& path)
{
    std::optional<sol::table> cls = load(path);
    if (!cls)
        return false; // the previous class, if any, stays live
    mFailed.erase(path);
    mClasses.insert_or_assign(path, std::move(*cls));
    return true;
}

void ScriptChunkCache::clear()
{
    mClasses.clear();
    mFailed.clear();
}

} // namespace eng::script
