#pragma once
#include "Dialogue.h"
#include "GameChannels.h"
#include "Hideout.h"
#include "Inventory.h"
#include "Items.h"
#include "LossPolicy.h"
#include "NpcSystem.h"
#include "Npcs.h"
#include "PickupSystem.h"
#include "Quests.h"
#include "RaidState.h"
#include "RpgSave.h"
#include "Skills.h"
#include "Stats.h"
#include "Trading.h"
#include "WorldState.h"

#include <entt/entt.hpp>

#include <functional>
#include <string>
#include <vector>

namespace game {
struct GameContext;
class CombatVocabulary;
}

// The one object the application owns.
//
// Everything above is a library or a pure system with no idea the others exist.
// This is where they are wired: it holds the content, the player's state, the
// channels the game raises on, and the small number of policies that need to
// see more than one of them at once (what a reward *is*, what a condition
// *means*, what happens to your loot when you die).
//
// It deliberately owns no rendering and no combat logic. `applyToPlayer` writes
// the derived maxima onto the combat components and stops; the combat systems
// go on being the authority on what a hit does.
namespace game::rpg {

class RpgRuntime {
public:
    struct Paths {
        std::string items = "config/items.toml";
        std::string loot = "config/loot.toml";
        std::string quests = "config/quests.toml";
        std::string dialogue = "config/dialogue.toml";
        std::string npcs = "config/npcs.toml";
        std::string progression = "config/progression.toml";
        std::string traders = "config/traders.toml";
        std::string stations = "config/stations.toml";
        // Where the profile is written. Under the user's data directory in a
        // shipped build; a path in the working directory is fine for now and
        // is what the debug panel's save button uses.
        std::string profile = "profile.rpg";
    };

    // Load everything and cross-validate. Returns false when the item table
    // itself failed -- the one file nothing else can work without. A missing
    // quest or dialogue file is survivable and logged: a dungeon with no
    // quests is a dungeon, a dungeon with no items is a bug.
    bool load(const Paths&, const CombatVocabulary&);
    // Re-read every file and rebind the live quests. For the debug panel.
    bool reload(const CombatVocabulary&);

    // --- the content -------------------------------------------------------
    const ItemLibrary& items() const { return mItems; }
    const LootLibrary& lootTables() const { return mLoot; }
    const QuestLibrary& questLibrary() const { return mQuestLibrary; }
    const DialogueLibrary& dialogueLibrary() const { return mDialogue; }
    const NpcLibrary& npcLibrary() const { return mNpcLibrary; }

    // --- the player --------------------------------------------------------
    CharacterSheet& sheet() { return mSheet; }
    const CharacterSheet& sheet() const { return mSheet; }
    SkillSet& skills() { return mSkills; }
    const SkillSet& skills() const { return mSkills; }
    const SkillTable& skillTable() const { return mSkillTable; }

    // Award skill experience for doing something, and keep the derived block
    // in step. This is the only way experience enters the game: there is no
    // "add level" anywhere, because a level is a reading of experience.
    void train(const std::string& skill, int64_t amount);
    // Experience that is not "you got better at something" -- extracting,
    // surviving, finishing a quest. Feeds the character level only.
    void awardCharacterXp(int64_t amount);
    Inventory& inventory() { return mInventory; }
    const Inventory& inventory() const { return mInventory; }
    QuestBook& quests() { return mQuests; }
    const QuestBook& quests() const { return mQuests; }
    WorldState& world() { return mWorld; }
    const WorldState& world() const { return mWorld; }
    PickupSystem& pickups() { return mPickups; }
    const PickupSystem& pickups() const { return mPickups; }
    NpcSystem& npcs() { return mNpcs; }
    const NpcSystem& npcs() const { return mNpcs; }
    DialogueRunner& conversation() { return mConversation; }
    const DialogueRunner& conversation() const { return mConversation; }
    GameChannels& channels() { return mChannels; }
    RaidState& raid() { return mRaid; }
    const RaidState& raid() const { return mRaid; }
    Market& market() { return mMarket; }
    const Market& market() const { return mMarket; }
    const TraderLibrary& traders() const { return mTraders; }
    Hideout& hideout() { return mHideout; }
    const Hideout& hideout() const { return mHideout; }
    const StationLibrary& stations() const { return mStations; }

    // --- the safehouse ------------------------------------------------------
    // Can the next tier of `station` be paid for right now, and why not.
    struct BuildCheck {
        bool ok = false;
        const StationTier* tier = nullptr;
        std::string blocker;
    };
    BuildCheck canBuild(const std::string& station) const;
    // Pay for it and apply it. Refuses outside the safehouse: upgrading your
    // house from the bottom of the dungeon is not a thing.
    bool build(const std::string& station);

    // --- the raid loop ------------------------------------------------------
    //
    // These wrap the state machine so the phase change and its consequences
    // (findings settling, the day turning, the profile being written) stay one
    // transaction. Nothing outside should drive RaidState directly.
    void beginRaid(int depth);
    void enterRaidLevel(int depth);
    void beginExtraction();
    void cancelExtraction();
    // Advance the extraction countdown. Returns true on the frame it lands, at
    // which point the extraction has already been applied and saved.
    bool tickRaid(float dt);
    void returnToSafehouse();

    // --- trading ------------------------------------------------------------
    struct TradeResult {
        bool ok = false;
        int coin = 0;
        std::string message;
    };
    // Sell `count` of `item` from the backpack to `trader`.
    TradeResult sell(const std::string& trader, const std::string& item,
                     int count);
    TradeResult buy(const std::string& trader, const std::string& item,
                    int count);
    // Hand over everything a barter wants and take what it gives.
    TradeResult barter(const std::string& trader, const std::string& barterId);
    // What the player would be paid / charged, for a shop UI.
    PriceQuote sellQuote(const std::string& trader,
                         const std::string& item) const;
    PriceQuote buyQuote(const std::string& trader,
                        const std::string& item) const;

    // --- what the game tells it --------------------------------------------
    //
    // These are the only entry points the rest of the game needs. Each raises
    // on a channel; who listens is not this class's business.

    // Something died. Grants experience, rolls the loot table, and drops what
    // it rolled at `at`. `lootTable` empty means no drops (still grants xp).
    void onEnemyKilled(GameContext&, const std::string& enemyId, int64_t xp,
                       const std::string& lootTable, glm::vec3 at);
    void onDepthReached(int depth);
    // Getting out. Settles ownership and banks the haul per the loss rules;
    // returns what was banked so the caller can show the run's takings.
    ExtractReport onExtracted(int depth);
    // The player died. Applies the loss rules and returns the full report --
    // what was taken, what survived and why, and what insurance paid back --
    // because the after-action screen is the only place those rules are ever
    // explained to a player.
    LossReport onPlayerDied(const std::string& killerId);

    // The dials the two edges above run on, from `[raid]` in progression.toml.
    const LossRules& lossRules() const { return mLossRules; }
    // What a death would cost right now. The inventory screen shows this while
    // the player is still deciding what to carry, which is the whole point of
    // the policy being a pure function.
    LossReport previewLoss() const;
    // Seal or release one stack against the next death. Returns false when the
    // seal would be wasted (the stack is already safe) or when every slot is
    // spent, both of which the screen reports rather than silently ignoring.
    bool setSecured(const std::string& item, bool secured);

    // --- inventory actions --------------------------------------------------
    // All of these keep the channels and the modifier layer in step, which is
    // why nothing outside should be touching Inventory directly.
    AddResult give(const std::string& item, int count, bool foundThisRun,
                   int* added = nullptr);
    int takeAway(const std::string& item, int count);
    bool equip(const std::string& item);
    bool unequip(EquipSlot);
    // Consume one and apply its [use] table to the player's components.
    bool useItem(entt::registry&, entt::entity player, const std::string& item);
    // Pick a world pickup up. Refuses (and leaves it in the world) when the
    // pack cannot take it.
    bool takePickup(GameContext&, int pickupId);
    bool canTake(int pickupId) const;

    // --- conversation -------------------------------------------------------
    bool talkTo(const std::string& npc);
    bool chooseReply(int offeredIndex);
    void endConversation();
    const std::string& conversationPartner() const { return mPartner; }

    // Remove every entity in `registry` whose SceneCondition does not hold.
    //
    // This is how the village changes: an entity gated on "the forge is lit" is
    // simply not built until it is. Destroying rather than hiding, because a
    // hidden entity still has a collider, still costs a draw call's worth of
    // culling, and is still findable by everything that walks the level looking
    // for something to interact with.
    //
    // Returns how many were removed, for the log line that tells a designer
    // their gate is working.
    int applySceneConditions(entt::registry& registry) const;

    // --- conditions and effects --------------------------------------------
    bool evaluate(const Condition&) const;
    void apply(const Effect&);

    // --- the seam to combat -------------------------------------------------
    // Write the derived maxima onto the player's combat components, preserving
    // each pool's current/max ratio. Called after anything that can change the
    // sheet; cheap enough to call on every equip, not per frame.
    void applyToPlayer(entt::registry&, entt::entity player) const;

    // --- persistence --------------------------------------------------------
    // Write the profile. Refuses, loudly, when the raid state says the player's
    // holdings are not settled -- there is no way to ask for a mid-raid save,
    // because that is the one thing that would erase the risk loop.
    bool saveTo(const std::string& path) const;
    bool loadFrom(const std::string& path);
    // The profile path from Paths, for the callers that just want "save".
    bool saveProfile() const { return saveTo(mPaths.profile); }
    bool loadProfile() { return loadFrom(mPaths.profile); }
    // Start a fresh character from progression.toml's [start] block.
    void newGame();

    // Set once, so a level transition can rebuild the pickups it authored.
    void setPickupsForLevel(GameContext&, const std::vector<ScenePlacement>&);
    // The same for the people the level stands in it.
    void setNpcsForLevel(GameContext&, const std::vector<ScenePlacement>&);

    // Open a conversation with the person under the crosshair. Resolves the
    // placement to a person, then to their conversation, so a shopkeeper who
    // shares a script with three other guards still talks.
    //
    // Returns false when there is nothing to say, which is a real state: a
    // trader with stock and no dialogue tree is somebody you shop with, not
    // somebody you chat to, and the caller opens the shop instead.
    bool talkToPlacement(int npcEntryId);

    // Human-readable one-liners for the last few things that happened, for the
    // HUD's message feed. Newest last.
    const std::vector<std::string>& log() const { return mLog; }
    void note(std::string line);

private:
    void wireRewards();
    void grantRewards(Quest&);
    void publishInventory(const std::string& item, int delta);

    void syncSkillModifiers();
    // Re-push the hideout's modifier group and re-apply its capacities.
    void syncHideout();

    Paths mPaths;
    LossRules mLossRules;
    SkillTable mSkillTable;
    SkillSet mSkills;
    TraderLibrary mTraders;
    StationLibrary mStations;
    Hideout mHideout;
    Market mMarket;
    RaidState mRaid;
    ItemLibrary mItems;
    LootLibrary mLoot;
    QuestLibrary mQuestLibrary;
    DialogueLibrary mDialogue;
    NpcLibrary mNpcLibrary;

    CharacterSheet mSheet;
    Inventory mInventory;
    QuestBook mQuests;
    WorldState mWorld;
    GameChannels mChannels;
    DialogueRunner mConversation;
    PickupSystem mPickups;
    NpcSystem mNpcs;
    std::string mPartner;

    // The loot stream. One per runtime, advanced by every roll, so an
    // expedition is reproducible from its seed.
    uint32_t mLootRng = 0x5EED1234u;
    // Where drops land, set by onEnemyKilled before it rolls.
    std::vector<std::string> mLog;
    // Starting kit, from progression.toml.
    std::vector<std::pair<std::string, int64_t>> mStartingSkills;
    std::vector<std::pair<std::string, int>> mStartingItems;
    std::vector<std::string> mStartingEquipment;
    int mStartingCurrency = 0;
    bool mLoaded = false;
};

} // namespace game::rpg
