#pragma once
#include <string>

namespace eng::script { class ScriptHost; }
namespace game::rpg { class RpgRuntime; }

// The game's typed event stream, delivered to Lua as `on_event`.
//
// GameChannels already carries everything a scripted encounter wants to know --
// something died, an item changed hands, a flag turned over, the run reached a
// depth, the player got out or did not. Every one of those had exactly one
// audience (the quest log) and no way for content to listen.
//
// This subscribes once and rebroadcasts, which is why it is a bridge and not a
// system: it owns no state, decides nothing, and adds no vocabulary. The event
// names are the channel names in snake_case, and the payload is always
// `{ subject = <id>, value = <number> }` -- so a script that has learned one
// event has learned all of them.
//
//   function Shrine:on_event(name, data)
//     if name == "enemy_killed" and data.subject == "cinder_thrall" then
//       self.lit = self.lit + data.value
//     end
//   end
//
// LIFETIME: it holds subscription ids and unsubscribes on destruction, so a
// level teardown that outlives the host cannot call into a dead Lua state. It
// must be destroyed before, or with, the RpgRuntime it listens to.
namespace game {

class ScriptEventBridge {
public:
    ~ScriptEventBridge();

    // Both references must outlive the bridge. Calling this twice detaches the
    // previous subscriptions first, so a level reload cannot double-deliver.
    void attach(eng::script::ScriptHost& host, rpg::RpgRuntime& rpg);
    void detach();
    bool attached() const { return mRpg != nullptr; }

private:
    eng::script::ScriptHost* mHost = nullptr;
    rpg::RpgRuntime* mRpg = nullptr;
    // One per channel. Zero means "not subscribed", which is what
    // Channel::kNoSubscription is.
    unsigned int mCombat = 0;
    unsigned int mInventory = 0;
    unsigned int mFlags = 0;
    unsigned int mNpcs = 0;
    unsigned int mDepth = 0;
    unsigned int mRaid = 0;
    unsigned int mQuestAssigned = 0;
    unsigned int mQuestCompleted = 0;
    unsigned int mQuestTurnedIn = 0;
    unsigned int mQuestFailed = 0;
};

} // namespace game
