#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

// The pipeline primitive: a typed, subscribable signal.
//
// Every game system that a quest can care about exposes one of these rather
// than being reached into. A quest subscribes when it becomes active and
// unsubscribes when it completes, so an inactive quest costs nothing per event
// and a completed one cannot fire twice.
//
// WHY NOT A STRING EVENT BUS
// AGENTS.md §28: "avoid string-based event names for critical behavior". A
// channel's signature *is* its contract -- `Channel<eng::StringId, int>` for
// enemies defeated -- so a quest that subscribes to the wrong thing does not
// compile, rather than silently never advancing.
//
// RE-ENTRANCY
// A handler may subscribe, unsubscribe, or raise while it runs: `raise` walks a
// snapshot of the slot list and re-checks each slot's liveness before calling
// it. That matters because the normal case *is* re-entrant -- a quest completes
// inside a handler, which unsubscribes it from the channel currently
// dispatching.
namespace game::rpg {

using SubscriptionId = std::uint32_t;
inline constexpr SubscriptionId kNoSubscription = 0;

template <class... Args> class Channel {
public:
    using Handler = std::function<void(Args...)>;

    SubscriptionId subscribe(Handler fn)
    {
        if (!fn)
            return kNoSubscription;
        const SubscriptionId id = ++mNextId;
        mSlots.push_back({id, std::move(fn)});
        return id;
    }

    void unsubscribe(SubscriptionId id)
    {
        if (id == kNoSubscription)
            return;
        for (Slot& s : mSlots) {
            if (s.id == id) {
                // Cleared rather than erased: a raise may be walking the list.
                // Dead slots are compacted on the next raise that is not
                // already dispatching.
                s.id = kNoSubscription;
                s.fn = nullptr;
                mHasDead = true;
                return;
            }
        }
    }

    void raise(Args... args) const
    {
        // By value: a handler that unsubscribes must not invalidate the walk.
        const std::vector<Slot> snapshot = mSlots;
        ++mDepth;
        for (const Slot& s : snapshot) {
            if (s.id == kNoSubscription || !s.fn)
                continue;
            // The snapshot can hold a slot a previous handler has since
            // cancelled, so liveness is re-checked against the live list.
            if (!live(s.id))
                continue;
            s.fn(args...);
        }
        --mDepth;
        if (mDepth == 0 && mHasDead)
            compact();
    }

    std::size_t subscriberCount() const
    {
        std::size_t n = 0;
        for (const Slot& s : mSlots)
            if (s.id != kNoSubscription)
                ++n;
        return n;
    }

    void clear()
    {
        mSlots.clear();
        mHasDead = false;
    }

private:
    struct Slot {
        SubscriptionId id = kNoSubscription;
        Handler fn;
    };

    bool live(SubscriptionId id) const
    {
        for (const Slot& s : mSlots)
            if (s.id == id)
                return true;
        return false;
    }

    void compact() const
    {
        mSlots.erase(std::remove_if(mSlots.begin(), mSlots.end(),
                                    [](const Slot& s) {
                                        return s.id == kNoSubscription;
                                    }),
                     mSlots.end());
        mHasDead = false;
    }

    // Mutable because raise() is const: raising is not a change to the
    // channel's meaning, and a system that owns a channel hands out a const
    // reference to whoever may only listen.
    mutable std::vector<Slot> mSlots;
    mutable bool mHasDead = false;
    mutable int mDepth = 0;
    SubscriptionId mNextId = kNoSubscription;
};

} // namespace game::rpg
