#include "RaidState.h"

#include <eng/Log.h>

#include <array>

namespace game::rpg {

namespace {
constexpr std::array<const char*, std::size_t(RaidPhase::Count)> kNames{
    "safehouse", "initialising", "active", "extracting", "extracted", "died"};
}

const char* nameOf(RaidPhase p)
{
    const auto i = std::size_t(p);
    return i < kNames.size() ? kNames[i] : "?";
}

void RaidState::transition(RaidPhase to)
{
    if (to == mPhase)
        return;
    const RaidPhase from = mPhase;
    mPhase = to;
    changed.raise(from, to);
}

void RaidState::beginRaid(int depth)
{
    if (inRaid()) {
        eng::log::error("raid: beginRaid while already in one (%s)",
                        nameOf(mPhase));
        return;
    }
    mDepth = depth;
    mExtractTimer = 0.0f;
    mLastKiller.clear();
    transition(RaidPhase::Initialising);
}

void RaidState::enterActive()
{
    if (mPhase != RaidPhase::Initialising && mPhase != RaidPhase::Extracting) {
        eng::log::error("raid: enterActive from %s", nameOf(mPhase));
        return;
    }
    mExtractTimer = 0.0f;
    transition(RaidPhase::Active);
}

void RaidState::beginExtraction()
{
    if (mPhase != RaidPhase::Active)
        return;
    mExtractTimer = mExtractDuration;
    transition(RaidPhase::Extracting);
}

void RaidState::cancelExtraction()
{
    if (mPhase != RaidPhase::Extracting)
        return;
    // Deliberately resets rather than pauses. Stepping out and back in must
    // cost the whole countdown again, or the countdown is not a commitment and
    // the extraction is not a decision.
    mExtractTimer = 0.0f;
    transition(RaidPhase::Active);
}

bool RaidState::tick(float dt)
{
    if (mPhase != RaidPhase::Extracting)
        return false;
    mExtractTimer -= dt;
    if (mExtractTimer > 0.0f)
        return false;
    mExtractTimer = 0.0f;
    transition(RaidPhase::Extracted);
    return true;
}

void RaidState::die(const std::string& killerId)
{
    // Legal from anywhere in a raid, including mid-extraction: being killed on
    // the last second of the countdown is the best story this system can tell,
    // and refusing the transition would silently save the run instead.
    if (!inRaid() && mPhase != RaidPhase::Initialising) {
        eng::log::error("raid: die() from %s", nameOf(mPhase));
        return;
    }
    mLastKiller = killerId;
    mExtractTimer = 0.0f;
    transition(RaidPhase::Died);
}

void RaidState::returnToSafehouse()
{
    mExtractTimer = 0.0f;
    transition(RaidPhase::Safehouse);
}

} // namespace game::rpg
