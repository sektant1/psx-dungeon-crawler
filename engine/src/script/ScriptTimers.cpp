#include "script/ScriptTimers.h"

#include <algorithm>

namespace eng::script {

TimerSet::Id TimerSet::add(float interval, bool repeats,
                           sol::protected_function fn)
{
    Timer t;
    t.id = mNextId++;
    t.interval = std::max(interval, 0.0f);
    // A repeating timer with a zero interval would fire forever inside one
    // tick. Clamping to one frame's worth makes `timer.every(0, ...)` mean
    // "every frame", which is what somebody writing it meant.
    if (repeats)
        t.interval = std::max(t.interval, 1.0f / 240.0f);
    t.remaining = t.interval;
    t.repeats = repeats;
    t.fn = std::move(fn);
    mTimers.push_back(std::move(t));
    return mTimers.back().id;
}

bool TimerSet::cancel(Id id)
{
    for (Timer& t : mTimers) {
        if (t.id != id || t.cancelled)
            continue;
        // Marked, not erased: cancel() can be called from inside a callback
        // that tick() is in the middle of, and erasing there would move the
        // element out from under it. The sweep at the end of tick() collects
        // these.
        t.cancelled = true;
        return true;
    }
    return false;
}

void TimerSet::tick(
    float dt, const std::function<void(const sol::protected_function&)>& call)
{
    if (mTimers.empty())
        return;

    // Collect first, then run. A callback may add timers (which must not fire
    // this frame) and cancel timers (including itself), and doing either while
    // walking mTimers would invalidate the walk.
    const std::size_t count = mTimers.size();
    std::vector<sol::protected_function> due;
    for (std::size_t i = 0; i < count; ++i) {
        Timer& t = mTimers[i];
        if (t.cancelled)
            continue;
        t.remaining -= dt;
        // Not `> 0`: accumulating dt in float means a 0.5s timer stepped ten
        // times by 0.05 lands a rounding error above zero and waits an entire
        // extra frame. A tenth of a millisecond is far below anything a game
        // can observe and well above the error a few hundred additions produce.
        if (t.remaining > 1e-4f)
            continue;
        due.push_back(t.fn);
        if (t.repeats) {
            // Carry the overshoot rather than resetting to the full interval,
            // so a 0.5s timer fires 120 times a minute regardless of frame
            // rate. Clamped so one long hitch does not queue a burst.
            t.remaining += t.interval;
            if (t.remaining < 0.0f)
                t.remaining = t.interval;
        } else {
            t.cancelled = true;
        }
    }

    mTicking = true;
    for (const sol::protected_function& fn : due)
        call(fn);
    mTicking = false;

    mTimers.erase(std::remove_if(mTimers.begin(), mTimers.end(),
                                 [](const Timer& t) { return t.cancelled; }),
                  mTimers.end());
}

} // namespace eng::script
