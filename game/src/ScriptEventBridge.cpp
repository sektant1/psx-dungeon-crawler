#include "ScriptEventBridge.h"

#include "rpg/RpgRuntime.h"

#include <eng/script/ScriptHost.h>

namespace game {
namespace {

// The event names, in one place.
//
// Scattering string literals through the subscription calls below would make
// "what can a script listen for" a question you answer by reading code. A game
// renaming or dropping one of these edits this table and nothing else -- which
// is also what makes the bridge reusable: none of its logic knows what a
// "cinder thrall" is, only that a channel carries a subject and a number.
struct EventNames {
    const char* enemyKilled = "enemy_killed";
    const char* itemChanged = "item_changed";
    const char* flagSet = "flag_set";
    const char* npcTalked = "npc_talked";
    const char* depthReached = "depth_reached";
    const char* extracted = "extracted";
    const char* raidPhase = "raid_phase";
    const char* questAssigned = "quest_assigned";
    const char* questCompleted = "quest_completed";
    const char* questTurnedIn = "quest_turned_in";
    const char* questFailed = "quest_failed";
};
constexpr EventNames kEvents{};

} // namespace

ScriptEventBridge::~ScriptEventBridge() { detach(); }

void ScriptEventBridge::attach(eng::script::ScriptHost& host,
                               rpg::RpgRuntime& rpg)
{
    detach();
    mHost = &host;
    mRpg = &rpg;
    rpg::GameChannels& channels = rpg.channels();

    const auto send = [&host](const char* name, const std::string& subject,
                              double value) {
        eng::script::EventData data;
        data.subject = subject;
        data.value = value;
        host.broadcast(name, data);
    };

    mCombat = channels.combat.subscribe(
        [send](eng::StringId enemy, int count) {
            send(kEvents.enemyKilled, enemy.c_str(), double(count));
        });

    // `held` rather than `delta` as the payload value: a script reacting to an
    // item almost always wants to know how many the player now has, and the
    // sign of the change is recoverable from two events where the total is not
    // recoverable from any number of deltas.
    mInventory = channels.inventory.subscribe(
        [send](eng::StringId item, int held, int) {
            send(kEvents.itemChanged, item.c_str(), double(held));
        });

    mFlags = channels.flags.subscribe([send](eng::StringId flag) {
        send(kEvents.flagSet, flag.c_str(), 1.0);
    });
    mNpcs = channels.npcs.subscribe([send](eng::StringId npc) {
        send(kEvents.npcTalked, npc.c_str(), 1.0);
    });

    // Two names off one channel, because "I went deeper" and "I got out alive"
    // are different things to a script and testing a boolean payload for the
    // difference is the kind of thing content gets wrong once and silently.
    mDepth = channels.depth.subscribe([send](int depth, bool extracted) {
        send(extracted ? kEvents.extracted : kEvents.depthReached, {},
             double(depth));
    });

    mRaid = rpg.raid().changed.subscribe(
        [send](rpg::RaidPhase, rpg::RaidPhase to) {
            send(kEvents.raidPhase, rpg::nameOf(to), 0.0);
        });

    mQuestAssigned = channels.quests.assigned.subscribe(
        [send](rpg::Quest& q) { send(kEvents.questAssigned, q.id(), 0.0); });
    mQuestCompleted = channels.quests.completed.subscribe(
        [send](rpg::Quest& q) { send(kEvents.questCompleted, q.id(), 0.0); });
    mQuestTurnedIn = channels.quests.turnedIn.subscribe(
        [send](rpg::Quest& q) { send(kEvents.questTurnedIn, q.id(), 0.0); });
    mQuestFailed = channels.quests.failed.subscribe(
        [send](rpg::Quest& q) { send(kEvents.questFailed, q.id(), 0.0); });
}

void ScriptEventBridge::detach()
{
    if (!mRpg) {
        mHost = nullptr;
        return;
    }
    rpg::GameChannels& channels = mRpg->channels();
    channels.combat.unsubscribe(mCombat);
    channels.inventory.unsubscribe(mInventory);
    channels.flags.unsubscribe(mFlags);
    channels.npcs.unsubscribe(mNpcs);
    channels.depth.unsubscribe(mDepth);
    mRpg->raid().changed.unsubscribe(mRaid);
    channels.quests.assigned.unsubscribe(mQuestAssigned);
    channels.quests.completed.unsubscribe(mQuestCompleted);
    channels.quests.turnedIn.unsubscribe(mQuestTurnedIn);
    channels.quests.failed.unsubscribe(mQuestFailed);
    mCombat = mInventory = mFlags = mNpcs = mDepth = mRaid = 0;
    mQuestAssigned = mQuestCompleted = mQuestTurnedIn = mQuestFailed = 0;
    mHost = nullptr;
    mRpg = nullptr;
}

} // namespace game
