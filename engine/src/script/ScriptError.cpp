#include "script/ScriptError.h"

#include <eng/Log.h>

namespace eng::script {
namespace {
// A registry key of our own, so this is not fighting anything Lua or sol2 keeps
// there under a name of theirs.
constexpr const char* kTracebackKey = "eng_script_traceback";
} // namespace

void installTracebackHandler(sol::state& lua)
{
    const sol::reference traceback = lua["debug"]["traceback"];
    lua.registry()[kTracebackKey] = traceback;
    // Every protected_function built from a callable on this state now picks
    // this up through sol2's get_default_handler.
    sol::protected_function::set_default_handler(traceback);
}

sol::reference tracebackHandler(sol::state& lua)
{
    return lua.registry()[kTracebackKey];
}

void reportScriptError(const std::string& path, const std::string& where,
                       const std::string& subject, const std::string& message)
{
    // One log call per error rather than one per line: the traceback is
    // multi-line, and splitting it would interleave it with whatever else is
    // logging that frame, which is exactly when you least want it scrambled.
    if (subject.empty())
        log::error("Script: %s in %s():\n  %s", path.c_str(), where.c_str(),
                   message.c_str());
    else
        log::error("Script: %s on %s in %s():\n  %s", path.c_str(),
                   subject.c_str(), where.c_str(), message.c_str());
}

} // namespace eng::script
