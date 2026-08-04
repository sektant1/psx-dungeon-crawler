#pragma once
#include <sol/sol.hpp>

#include <optional>
#include <string>
#include <unordered_map>

namespace eng::script {

// Where a script named by logical path ("scripts/door.lua") actually lives.
// Prefers the path as it stands, then falls back to eng::assets::resolve, so a
// test can pass an absolute path while a scene passes a portable one.
//
// Scripts are always *keyed* by their logical path -- that is what the cache,
// the reload command and the traceback all use -- and this is only for opening
// the file.
std::string resolveScriptPath(const std::string& path);

// Logical script path -> the class table the chunk returned.
//
// A .lua file is a chunk that returns a table: its *class*. It is run exactly
// once per path no matter how many entities carry it, and every instance
// reaches its methods through __index. That is what makes attaching the same
// script to two hundred entities cost two hundred small tables and one chunk.
class ScriptChunkCache {
public:
    explicit ScriptChunkCache(sol::state& lua) : mLua(lua) {}

    // The class table for `path`, loading it on first request. Returns nullptr
    // on any failure -- unreadable, does not parse, raises while running, or
    // does not return a table -- having already reported it. A failure is
    // remembered, so a broken script reports once rather than once per entity
    // per frame.
    //
    // The returned pointer is stable until reload() or clear() touches THAT
    // path: the map owns the table and node-based storage does not move it.
    sol::table* classFor(const std::string& path);

    // Re-runs the chunk and replaces the class table in place. Returns false
    // and KEEPS the previous class when the new source fails -- a half-typed
    // save must not kill a running level.
    bool reload(const std::string& path);

    void clear();

private:
    // Reads the file and runs it. Reports and returns nullopt on failure.
    std::optional<sol::table> load(const std::string& path);

    sol::state& mLua;
    std::unordered_map<std::string, sol::table> mClasses;
    // Paths whose last load failed, so classFor() does not retry and re-report
    // every frame. reload() clears the entry.
    std::unordered_map<std::string, bool> mFailed;
};

} // namespace eng::script
