#pragma once
#include <sol/sol.hpp>

#include <string>

namespace eng::script {

// Makes debug.traceback the message handler for every protected call on this
// state. Call once, before anything is loaded.
//
// Without it a failure reports only the line that raised -- which, for a script
// that calls a helper that calls a binding, tells you nothing about how it got
// there. With it the report carries the whole Lua call stack.
//
// Installed as sol2's *default* handler rather than assigned at each call site.
// sol2 3.x makes the handler constructor-only (it was an assignable member in
// 2.x), so a per-site assignment would be a line every future callback has to
// remember; a default cannot be forgotten.
void installTracebackHandler(sol::state& lua);

// The handler installed above, for constructing a protected_function
// explicitly: sol::protected_function fn(callable, tracebackHandler(lua)).
sol::reference tracebackHandler(sol::state& lua);

// One error, one report. `where` is the callback or phase ("update", "load");
// `subject` names the entity when there is one ("entity 'iron_door' #42") and
// is empty at load time, when no entity is involved yet.
//
// Logged through eng::log::error behind a "Script:" prefix, which is what files
// it under the DebugConsole's `Script` category -- the console derives a
// category from a leading Word: prefix.
void reportScriptError(const std::string& path, const std::string& where,
                       const std::string& subject, const std::string& message);

} // namespace eng::script
