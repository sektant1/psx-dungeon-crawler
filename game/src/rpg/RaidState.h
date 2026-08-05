#pragma once
#include "Channel.h"

#include <string>

// The raid state machine: the thing that makes an expedition risky.
//
//   Initialising -> Active -> ExtractionCountdown -> Extracted
//                     |                |
//                     +--> Died <------+
//
// Its whole job is to decide *when the game is allowed to write to disk*. A
// single-player extraction game with no server authority has exactly one
// defence against save-scumming away the tension, and it is this: the profile
// is serialised on a successful extraction or a return to the safehouse, and on
// death, and at no other time. A quicksave in the middle of a raid would erase
// the entire risk loop, so there is nowhere in this API to ask for one.
//
// It owns no inventory and no world. It announces phase changes; RpgRuntime
// listens and decides what a phase change *does* to the stash.
namespace game::rpg {

enum class RaidPhase {
    Safehouse,   // in the hub; the stash is reachable and writes are safe
    Initialising,// loadout chosen, level being built
    Active,      // in the dungeon, everything carried is provisional
    Extracting,  // stood in an exit, the countdown is running
    Extracted,   // made it out; findings become owned
    Died,        // did not; findings are lost
    Count
};
const char* nameOf(RaidPhase);

class RaidState {
public:
    // Fired on every transition, with (from, to). One channel rather than a
    // channel per phase: a listener nearly always cares about the edge, and a
    // switch on the pair is how it reads.
    Channel<RaidPhase, RaidPhase> changed;

    RaidPhase phase() const { return mPhase; }
    bool inRaid() const
    {
        return mPhase == RaidPhase::Active || mPhase == RaidPhase::Extracting;
    }
    // The one question the serialisation manager asks. True only in the states
    // where the player's holdings are settled and a write cannot be used to
    // undo a loss.
    bool mayPersist() const
    {
        return mPhase == RaidPhase::Safehouse || mPhase == RaidPhase::Extracted ||
               mPhase == RaidPhase::Died;
    }

    int depth() const { return mDepth; }
    void setDepth(int d) { mDepth = d; }
    float extractionRemaining() const { return mExtractTimer; }
    float extractionDuration() const { return mExtractDuration; }
    void setExtractionDuration(float s) { mExtractDuration = s > 0.0f ? s : 0.0f; }

    // Transitions. Each refuses the ones that are not legal from where it is,
    // and says so, rather than teleporting the machine into a state the rest of
    // the game is not expecting.
    void beginRaid(int depth);
    void enterActive();
    // Stood in an extraction zone. Starts (or continues) the countdown.
    void beginExtraction();
    // Left the zone before the countdown finished.
    void cancelExtraction();
    void die(const std::string& killerId);
    void returnToSafehouse();

    // Advances the extraction countdown. Returns true on the frame it
    // completes, which is the caller's cue to run the extraction.
    bool tick(float dt);

    const std::string& lastKiller() const { return mLastKiller; }

private:
    void transition(RaidPhase to);

    RaidPhase mPhase = RaidPhase::Safehouse;
    int mDepth = 0;
    float mExtractDuration = 4.0f;
    float mExtractTimer = 0.0f;
    std::string mLastKiller;
};

} // namespace game::rpg
