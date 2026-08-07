#pragma once
#include <sol/sol.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace eng::script {

// Deferred and repeating Lua callbacks: `timer.after` and `timer.every`.
//
// Owned by the host and ticked with the game clock rather than left to scripts
// to count down themselves, for two reasons. A script counting its own `self.t`
// keeps working until somebody pauses the game or slows time, at which point
// every hand-rolled timer in the project is wrong in a different way. And a
// timer that fires exactly once has to be cancelled from inside its own
// callback, which is the case hand-rolled versions always get wrong.
//
// Timers are NOT entity-scoped. A callback is a Lua closure and keeps its
// upvalues alive, so one that outlives the entity that created it still runs
// safely -- a LuaEntity re-checks validity before touching anything, so a
// timer firing on a destroyed entity is a no-op rather than a crash. Scoping
// them to entities would mean a door script could not schedule anything that
// survives the door, which is a real thing to want.
class TimerSet {
public:
    using Id = std::uint64_t;

    // `interval` <= 0 fires on the next tick. `repeats` reschedules by the same
    // interval after each firing.
    Id add(float interval, bool repeats, sol::protected_function fn);

    // True if it was live. Cancelling an already-fired one-shot, or an id from
    // a previous level, is not an error -- a script should not have to track
    // which of those it is holding.
    bool cancel(Id id);

    // Fires everything due, in creation order. `dt` is game time, so a paused
    // clock stops every timer in the project at once.
    //
    // A callback may add or cancel timers, including its own, which is why the
    // due set is collected before any of it runs: mutating the list while
    // walking it is how this kind of thing corrupts itself.
    void tick(float dt, const std::function<void(const sol::protected_function&)>& call);

    void clear() { mTimers.clear(); }
    std::size_t size() const { return mTimers.size(); }

private:
    struct Timer {
        Id id = 0;
        float remaining = 0.0f;
        float interval = 0.0f;
        bool repeats = false;
        bool cancelled = false;
        sol::protected_function fn;
    };

    std::vector<Timer> mTimers;
    Id mNextId = 1;
    // Guards against a callback that adds a timer while tick() is walking the
    // list: additions land in mTimers and must not be considered due this
    // frame, or `timer.every(0, ...)` would recurse until the stack ran out.
    bool mTicking = false;
};

} // namespace eng::script
