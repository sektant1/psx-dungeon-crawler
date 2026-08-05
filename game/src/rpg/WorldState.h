#pragma once
#include "RpgTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// What the world remembers, and how it is told that something happened.
//
// Two things live here because they are two halves of one idea: the durable
// facts (flags, counters, how each villager feels about you, what day it is)
// and the typed stream that changes them. Quests, NPC progression and the
// tutorial all read the same stream; none of them poll.
namespace game::rpg {

// The event bus.
//
// Publish-and-drain rather than publish-and-dispatch: an effect that fires
// during objective evaluation (a quest completing, which starts another quest,
// which grants an item, which is itself an ItemAcquired event) must not
// re-enter the evaluator halfway through its own loop. Events queue, the frame
// drains them in order, and anything published while draining lands on the next
// pass -- bounded, so a content cycle costs frames rather than the stack.
class EventBus {
public:
    void publish(const GameEvent& e) { mQueue.push_back(e); }
    void publish(EventKind kind, std::string_view subject, int count = 1)
    {
        mQueue.push_back({kind, subject.empty() ? eng::StringId{}
                                                : eng::intern(subject),
                          count});
    }

    // Hand the queued events to the caller and clear it. Returns a copy so a
    // subscriber may publish while it processes.
    std::vector<GameEvent> drain();
    bool pending() const { return !mQueue.empty(); }
    void clear() { mQueue.clear(); }

    // Everything published since the last reset, newest last. For the debug
    // panel: "why did that quest not advance" is nearly always answered by
    // looking at what was actually published.
    const std::vector<GameEvent>& history() const { return mHistory; }
    void clearHistory() { mHistory.clear(); }

private:
    static constexpr std::size_t kHistoryMax = 256;
    std::vector<GameEvent> mQueue;
    std::vector<GameEvent> mHistory;
};

// The durable facts.
//
// Flags are stored by name rather than by hashed id because they are saved, and
// a save that stores only hashes cannot be inspected, migrated or debugged. The
// hash is what runtime comparison uses; the string is what persistence and the
// panel use.
class WorldState {
public:
    // --- flags --------------------------------------------------------------
    void setFlag(const std::string& flag, bool value = true);
    void clearFlag(const std::string& flag) { setFlag(flag, false); }
    bool flag(const std::string& flag) const;
    const std::vector<std::string>& flags() const { return mFlags; }

    // --- counters -----------------------------------------------------------
    // Arbitrary named tallies: how many times a boss has been fought, how much
    // of a reagent has ever been delivered. Quest objectives use their own
    // progress; this is for the world's own bookkeeping.
    void addCounter(const std::string& key, int delta);
    int counter(const std::string& key) const;
    const std::unordered_map<std::string, int>& counters() const
    {
        return mCounters;
    }

    // --- NPC standing -------------------------------------------------------
    // One integer per character. AGENTS.md §16 is explicit that major NPC
    // progression "must not be interchangeable reputation bars", so this is
    // deliberately *not* the progression system: it is the cheap axis (does
    // this person deal with you at all) that a character-specific system sits
    // on top of.
    void addStanding(const std::string& npc, int delta);
    int standing(const std::string& npc) const;
    const std::unordered_map<std::string, int>& standings() const
    {
        return mStanding;
    }

    // --- time ---------------------------------------------------------------
    // §17: time advances through expeditions, deaths and story actions, never
    // through real-time waiting. Nothing here ticks on a clock.
    int day() const { return mDay; }
    void advanceDay(int days = 1);
    void setDay(int d) { mDay = d < 0 ? 0 : d; }

    // --- expedition ---------------------------------------------------------
    int deepestDepth() const { return mDeepestDepth; }
    void noteDepth(int depth);

    void clear();

private:
    // Set flags only. A cleared flag is erased rather than stored as false, so
    // "is it set" is a lookup and a save carries no tombstones.
    std::vector<std::string> mFlags;
    std::unordered_map<std::string, int> mCounters;
    std::unordered_map<std::string, int> mStanding;
    int mDay = 0;
    int mDeepestDepth = 0;
};

} // namespace game::rpg
