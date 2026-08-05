#pragma once
#include "GameChannels.h"
#include "RpgTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Quests: what someone asked you to do, and how far along you are.
//
// ARCHITECTURE
// This follows the scalable-quest-system pattern: an abstract `Quest` owns the
// lifecycle (Pending -> Active -> Complete -> TurnedIn) and the parts every
// quest has -- id, name, requirements, rewards, a reference to the quest
// channel -- while a *subclass per kind of goal* owns the progression logic and
// its own state. A quest subscribes to the channel it cares about in
// `onActivate` and unsubscribes when it leaves the active state, so an
// inactive quest costs nothing per event and a completed one cannot fire twice.
//
// Rewards are not handed out here. `Quest::complete()` raises
// QuestChannel::completed and stops; whoever owns experience, coin and the
// inventory subscribes and takes what it recognises. That is what keeps the
// quest layer from depending on the character sheet.
//
// AGENTS.md §28 asks for exactly this shape -- "explicit stateful objects or
// components for bosses, quests, NPC storylines" -- as against the
// data-oriented storage it wants for projectiles and simple enemies.
//
// WHY IT IS STILL DATA-DRIVEN
// A subclass is a *kind* of goal, not a quest. `[quest.<id>] type = "collect"`
// picks the subclass and the rest of the row parameterises it, so adding the
// eleventh fetch quest is a TOML table and adding a genuinely new kind of goal
// -- "observe this enemy without killing it", which §20 asks for -- is one
// class and one line in the factory. Content authors never touch C++; the one
// person adding a new verb does.
namespace game::rpg {

// The authored row. Everything a quest of any kind can carry; a subclass reads
// the fields it understands and ignores the rest.
struct QuestDef {
    std::string id;
    std::string type = "collect"; // selects the subclass; see makeQuest
    std::string title = "Untitled";
    std::string summary;
    std::string giver;       // npc id, for the log and for turn-in gating
    std::string objectiveText; // the log line; a subclass supplies a default
    bool autoTurnIn = false;   // resolves itself; no conversation needed
    bool repeatable = false;

    // Must all pass before the quest may be assigned.
    std::vector<Condition> requirements;

    // The subclass's parameters. `target` is the thing (an item id, an enemy
    // id, an npc id, a flag); `amount` is how many. Two fields rather than a
    // per-subclass struct, because every goal this game has needs exactly a
    // subject and a count -- and a subclass that needs more reads `extra`.
    std::string target;
    int amount = 1;
    std::string extra;

    struct Rewards {
        int64_t xp = 0;
        int currency = 0;
        std::vector<std::pair<std::string, int>> items; // id, count
        std::vector<Effect> effects; // anything beyond loot
    } rewards;
};

class QuestLibrary {
public:
    bool load(const std::string& tomlPath);
    bool loadFromString(const char* tomlSrc);

    const QuestDef* find(const std::string& id) const;
    std::vector<std::string> ids() const;
    int size() const { return int(mQuests.size()); }
    // Every item id any quest hands out or asks for, so a caller can validate
    // against the item library at load rather than at turn-in.
    std::vector<std::string> referencedItems() const;

private:
    bool parse(const void* tomlTable);
    std::unordered_map<std::string, QuestDef> mQuests;
};

// Pending is the assignable-but-not-assigned state the reference architecture
// names; this game never leaves a quest sitting in it (assignment activates
// immediately), but restoring a save and failing a quest both need somewhere
// that is not Active.
enum class QuestState { Pending, Active, Complete, TurnedIn, Failed, Count };
const char* nameOf(QuestState);

// The abstract quest.
//
// Lifecycle, and who calls what:
//   QuestBook::assign  -> enable()   -> onActivate(channels): subscribe
//   channel event      -> the subclass's handler -> tryComplete()
//   tryComplete        -> complete() -> disable(), raise QuestChannel::completed
//   QuestBook::turnIn  ->               raise QuestChannel::turnedIn
class Quest {
public:
    Quest(const QuestDef& def, QuestChannel& channel)
        : mDef(&def), mChannel(&channel)
    {
    }
    virtual ~Quest() { disable(); }

    Quest(const Quest&) = delete;
    Quest& operator=(const Quest&) = delete;

    const std::string& id() const { return mDef->id; }
    const std::string& title() const { return mDef->title; }
    const QuestDef& def() const { return *mDef; }
    QuestState state() const { return mState; }
    const std::vector<Condition>& requirements() const
    {
        return mDef->requirements;
    }
    const QuestDef::Rewards& rewards() const { return mDef->rewards; }

    // Enter Active and subscribe. Idempotent: enabling an already-active quest
    // does not double-subscribe.
    void enable(GameChannels& channels);
    // Leave the channels. Called on completion, failure and destruction, and
    // safe to call when never enabled.
    void disable();

    // --- progress, for the log UI and the save ------------------------------
    virtual int progress() const = 0;
    virtual int target() const = 0;
    // Restore a counter from a save. The subclass decides what a count means;
    // this is the one place the outside world writes it.
    virtual void setProgress(int) = 0;
    // The log line. The default is "<objective text> (n/m)"; a subclass that
    // has something better to say overrides it.
    virtual std::string progressText() const;

    // Force the quest into Complete regardless of progress -- for the quests a
    // conversation resolves, and for a restored save.
    void forceComplete();
    void markTurnedIn();
    void fail();
    void restoreState(QuestState s) { mState = s; }

protected:
    // The reference architecture's QuestActive(): the subclass subscribes here
    // and records its subscriptions so disable() can undo them.
    virtual void onActivate(GameChannels& channels) = 0;
    // Called by the subclass after it changes its counter.
    void tryComplete();
    virtual bool satisfied() const = 0;

    // Subscription bookkeeping the subclasses share, so no subclass has to
    // remember to write an unsubscribe path.
    void track(SubscriptionId id, std::function<void(SubscriptionId)> cancel);

    const QuestDef* mDef = nullptr;

private:
    void complete();

    struct Tracked {
        SubscriptionId id = kNoSubscription;
        std::function<void(SubscriptionId)> cancel;
    };

    QuestChannel* mChannel = nullptr;
    std::vector<Tracked> mTracked;
    QuestState mState = QuestState::Pending;
};

// --- the concrete goals ----------------------------------------------------
//
// Each is small on purpose. A goal that cannot be written in twenty lines is
// usually two goals.

// "Have N of an item." Tests the *holding*, not the acquisitions, so spending
// what you collected un-satisfies it until you collect again -- which is what
// makes a delivery quest work: you carry it to the giver and hand it over.
class CollectItemQuest : public Quest {
public:
    using Quest::Quest;
    int progress() const override { return mHeld; }
    int target() const override { return mDef->amount; }
    void setProgress(int v) override { mHeld = v; }

protected:
    void onActivate(GameChannels&) override;
    bool satisfied() const override { return mHeld >= mDef->amount; }

private:
    int mHeld = 0;
};

// "Defeat N of an enemy." Counts events; an empty target counts anything.
class DefeatEnemiesQuest : public Quest {
public:
    using Quest::Quest;
    int progress() const override { return mDefeated; }
    int target() const override { return mDef->amount; }
    void setProgress(int v) override { mDefeated = v; }

protected:
    void onActivate(GameChannels&) override;
    bool satisfied() const override { return mDefeated >= mDef->amount; }

private:
    int mDefeated = 0;
};

// "Hand N of an item to someone." Watches for the holding to *drop* while the
// giver is being spoken to, which is what distinguishes delivering from
// dropping it down a well. `extra` names the npc; empty means anyone.
class DeliverItemQuest : public Quest {
public:
    using Quest::Quest;
    int progress() const override { return mDelivered; }
    int target() const override { return mDef->amount; }
    void setProgress(int v) override { mDelivered = v; }
    // Called by the runtime when a conversation opens, so the quest knows a
    // subsequent loss is a hand-over rather than a loss.
    void setNearGiver(bool v) { mNearGiver = v; }

protected:
    void onActivate(GameChannels&) override;
    bool satisfied() const override { return mDelivered >= mDef->amount; }

private:
    int mDelivered = 0;
    bool mNearGiver = false;
};

// "Reach depth N", or "come back from depth N alive" when `extra` is
// "extract". The expedition goal §14 is built out of.
class ReachDepthQuest : public Quest {
public:
    using Quest::Quest;
    int progress() const override { return mBest; }
    int target() const override { return mDef->amount; }
    void setProgress(int v) override { mBest = v; }

protected:
    void onActivate(GameChannels&) override;
    bool satisfied() const override { return mBest >= mDef->amount; }

private:
    int mBest = 0;
};

// "Speak to someone." The verb every investigation is made of, and the one that
// makes "observe an enemy instead of killing it" expressible: a trigger sets a
// flag, the flag quest below sees it.
class TalkToNpcQuest : public Quest {
public:
    using Quest::Quest;
    int progress() const override { return mTalked; }
    int target() const override { return mDef->amount; }
    void setProgress(int v) override { mTalked = v; }

protected:
    void onActivate(GameChannels&) override;
    bool satisfied() const override { return mTalked >= mDef->amount; }

private:
    int mTalked = 0;
};

// "This flag became true." The escape hatch: anything a trigger volume, a
// script or a boss can say happened is a flag, and a quest can wait on it
// without the quest system growing a case for it.
class FlagQuest : public Quest {
public:
    using Quest::Quest;
    int progress() const override { return mSet ? 1 : 0; }
    int target() const override { return 1; }
    void setProgress(int v) override { mSet = v > 0; }

protected:
    void onActivate(GameChannels&) override;
    bool satisfied() const override { return mSet; }

private:
    bool mSet = false;
};

// Build the quest object a definition asks for. Returns null (and logs) for an
// unknown `type`, which is the one content error this system cannot recover
// from -- a quest with no logic would sit Active forever.
std::unique_ptr<Quest> makeQuest(const QuestDef&, QuestChannel&);
// The `type` names makeQuest accepts, for validation and the debug panel.
std::vector<std::string> questTypes();

// The player's quest log: the live Quest objects, and the assignment policy.
//
// It does not own the library -- definitions hot-reload and a live quest holds
// a pointer to its row, so a reload rebuilds the book rather than dangling it.
class QuestBook {
public:
    void bind(GameChannels& channels) { mChannels = &channels; }

    using ConditionFn = std::function<bool(const Condition&)>;

    // Assign a quest, checking its requirements when an evaluator is given.
    // Raises QuestChannel::assigned on success.
    bool assign(const QuestLibrary&, const std::string& id, const ConditionFn&);
    bool assignUnchecked(const QuestLibrary&, const std::string& id);

    // Collect the rewards of a completed quest. Returns the quest so the caller
    // can read `rewards()`, or null when it is not ready.
    Quest* turnIn(const std::string& id);
    bool fail(const std::string& id);
    bool forceComplete(const QuestLibrary&, const std::string& id);

    Quest* find(const std::string& id);
    const Quest* find(const std::string& id) const;
    QuestState stateOf(const std::string& id) const;
    bool isActive(const std::string& id) const;
    bool isFinished(const std::string& id) const; // Complete or TurnedIn
    bool isStarted(const std::string& id) const;

    const std::vector<std::unique_ptr<Quest>>& quests() const { return mQuests; }
    // Rebuild every live quest against a reloaded library, preserving state and
    // progress by id. A quest whose row is gone is dropped and reported.
    std::vector<std::string> rebind(const QuestLibrary&);
    void clear();

    // Tell every delivery quest whether the player is talking to their giver.
    void setConversationPartner(const std::string& npc);

private:
    GameChannels* mChannels = nullptr;
    std::vector<std::unique_ptr<Quest>> mQuests;
};

// The condition/effect array readers, shared with Dialogue.cpp.
//
// `table` is a `const toml::table*` behind a void* so this header does not drag
// toml++ into everything that includes it -- the same trick ItemLibrary::parse
// and EnemyLibrary::parse already use for exactly that reason.
std::vector<Condition> readConditionArray(const void* table, const char* key,
                                          const std::string& owner);
std::vector<Effect> readEffectArray(const void* table, const char* key,
                                    const std::string& owner);

} // namespace game::rpg
