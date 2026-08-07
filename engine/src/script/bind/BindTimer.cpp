#include "script/bind/Bindings.h"

#include "script/ScriptTimers.h"

namespace eng::script {

void bindTimers(sol::state& lua, TimerSet& timers)
{
    sol::table t = lua.create_named_table("timer");

    // Both return an id. Keeping it is optional for `after` -- a one-shot
    // cleans itself up -- and the point of `every`, which otherwise runs for
    // the life of the level.
    t["after"] = [&timers](float seconds, sol::protected_function fn) {
        return timers.add(seconds, /*repeats=*/false, std::move(fn));
    };
    t["every"] = [&timers](float seconds, sol::protected_function fn) {
        return timers.add(seconds, /*repeats=*/true, std::move(fn));
    };
    t["cancel"] = [&timers](TimerSet::Id id) { return timers.cancel(id); };
    t["count"] = [&timers]() { return timers.size(); };
}

} // namespace eng::script
