#pragma once
#include <sol/sol.hpp>

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eng::script {

// One live script on one entity.
struct ScriptInstance {
    entt::entity entity = entt::null;
    std::string path;
    sol::table self;          // per-entity state; __index -> the class table
    bool started = false;     // start() has run
    bool quarantined = false; // errored once; skipped until revived
    bool alive = false;       // slot is in use
};

// Slot-allocated instances with a free list.
//
// A pool rather than a map keyed by entity: ScriptState stores plain uint32_t
// slots, which is what keeps the VM out of a component header. Slots are reused
// and carry no generation counter, because a ScriptState is destroyed with its
// entity and nothing outside the host holds a slot across that.
class ScriptInstancePool {
public:
    uint32_t create(entt::entity e, std::string path, sol::table self);

    // nullptr when the slot is free, so a caller walking a stale ScriptState
    // cannot resurrect one.
    ScriptInstance* get(uint32_t slot);
    const ScriptInstance* get(uint32_t slot) const;

    void release(uint32_t slot);
    void clear();
    std::size_t liveCount() const { return mLive; }

    // Every live slot. Used by the tick loops, reload and the console listing.
    template <typename Fn> void forEach(Fn&& fn)
    {
        for (uint32_t i = 0; i < uint32_t(mSlots.size()); ++i)
            if (mSlots[i].alive) fn(i, mSlots[i]);
    }
    template <typename Fn> void forEach(Fn&& fn) const
    {
        for (uint32_t i = 0; i < uint32_t(mSlots.size()); ++i)
            if (mSlots[i].alive) fn(i, mSlots[i]);
    }

private:
    std::vector<ScriptInstance> mSlots;
    std::vector<uint32_t> mFree;
    std::size_t mLive = 0;
};

} // namespace eng::script
