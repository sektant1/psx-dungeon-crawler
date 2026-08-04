#include "script/ScriptInstance.h"

namespace eng::script {

uint32_t ScriptInstancePool::create(entt::entity e, std::string path,
                                    sol::table self)
{
    uint32_t slot;
    if (!mFree.empty()) {
        slot = mFree.back();
        mFree.pop_back();
    } else {
        slot = uint32_t(mSlots.size());
        mSlots.emplace_back();
    }
    ScriptInstance& inst = mSlots[slot];
    inst.entity = e;
    inst.path = std::move(path);
    inst.self = std::move(self);
    inst.started = false;
    inst.quarantined = false;
    inst.alive = true;
    ++mLive;
    return slot;
}

ScriptInstance* ScriptInstancePool::get(uint32_t slot)
{
    if (slot >= mSlots.size() || !mSlots[slot].alive) return nullptr;
    return &mSlots[slot];
}

const ScriptInstance* ScriptInstancePool::get(uint32_t slot) const
{
    if (slot >= mSlots.size() || !mSlots[slot].alive) return nullptr;
    return &mSlots[slot];
}

void ScriptInstancePool::release(uint32_t slot)
{
    if (slot >= mSlots.size() || !mSlots[slot].alive) return;
    ScriptInstance& inst = mSlots[slot];
    inst.alive = false;
    inst.started = false;
    // Drop the Lua reference explicitly. Leaving it would keep the instance
    // table -- and everything it captured -- alive until the slot happens to be
    // reused, which is a leak with an unbounded lifetime on a level that never
    // reloads.
    inst.self = sol::lua_nil;
    inst.path.clear();
    inst.entity = entt::null;
    mFree.push_back(slot);
    --mLive;
}

void ScriptInstancePool::clear()
{
    mSlots.clear();
    mFree.clear();
    mLive = 0;
}

} // namespace eng::script
