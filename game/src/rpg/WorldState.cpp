#include "WorldState.h"

#include <algorithm>

namespace game::rpg {

std::vector<GameEvent> EventBus::drain()
{
    std::vector<GameEvent> out;
    out.swap(mQueue);
    mHistory.insert(mHistory.end(), out.begin(), out.end());
    if (mHistory.size() > kHistoryMax)
        mHistory.erase(mHistory.begin(),
                       mHistory.begin() +
                           long(mHistory.size() - kHistoryMax));
    return out;
}

void WorldState::setFlag(const std::string& flag, bool value)
{
    if (flag.empty())
        return;
    const auto it = std::find(mFlags.begin(), mFlags.end(), flag);
    if (value) {
        if (it == mFlags.end())
            mFlags.push_back(flag);
    } else if (it != mFlags.end()) {
        mFlags.erase(it);
    }
}

bool WorldState::flag(const std::string& f) const
{
    return std::find(mFlags.begin(), mFlags.end(), f) != mFlags.end();
}

void WorldState::addCounter(const std::string& key, int delta)
{
    if (key.empty())
        return;
    mCounters[key] += delta;
}

int WorldState::counter(const std::string& key) const
{
    const auto it = mCounters.find(key);
    return it == mCounters.end() ? 0 : it->second;
}

void WorldState::addStanding(const std::string& npc, int delta)
{
    if (npc.empty())
        return;
    // Clamped rather than unbounded: standing gates conversations, and a
    // character who has been helped two hundred times should not be
    // arithmetically incapable of being disappointed.
    int& v = mStanding[npc];
    v = std::clamp(v + delta, -100, 100);
}

int WorldState::standing(const std::string& npc) const
{
    const auto it = mStanding.find(npc);
    return it == mStanding.end() ? 0 : it->second;
}

void WorldState::advanceDay(int days)
{
    if (days > 0)
        mDay += days;
}

void WorldState::noteDepth(int depth)
{
    mDeepestDepth = std::max(mDeepestDepth, depth);
}

void WorldState::clear()
{
    mFlags.clear();
    mCounters.clear();
    mStanding.clear();
    mDay = 0;
    mDeepestDepth = 0;
}

} // namespace game::rpg
