#pragma once
#include "Hideout.h"
#include "Inventory.h"
#include "Quests.h"
#include "Skills.h"
#include "Stats.h"
#include "Trading.h"
#include "WorldState.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Persistence for the RPG layer.
//
// Same shape as enemy/EnemySave.h, deliberately: a plain snapshot struct, a
// *pure* codec over it, and the capture/restore that need a live game. The
// codec compiles and round-trips with no renderer, no physics and no registry,
// so the format can be exercised exhaustively and cheaply -- and a save that
// loads wrong is worse than one that refuses to load, so `decode` refuses
// anything it does not recognise rather than reading a field that has moved.
//
// WHAT IS SAVED
//   - progression: experience per skill, and the accumulated character
//     experience the Tarkov-style level is read from
//   - the backpack, the stash, and what is worn -- with provenance, so a save
//     made mid-expedition still knows which loot a death would take
//   - currency
//   - every quest the player has touched, and each objective's progress
//   - world flags, counters, NPC standing, the day, the deepest depth reached
//
// WHAT IS DELIBERATELY NOT SAVED, and why
//   - Derived stats. They are a pure function of (base, curve, modifiers) and
//     writing them down is how a save starts disagreeing with the content it
//     was made against. They are recomputed on load.
//   - The equipment modifier layer. Same argument: it is rebuilt from what is
//     worn, so a rebalanced breastplate takes effect on the next load instead
//     of being frozen into every existing save.
//   - Timed effects (potions, blessings). A save is a state boundary; a buff
//     with 4 seconds left is not state worth carrying across one.
//   - The conversation in progress. A save landing mid-dialogue restores at the
//     tree's entry, which re-evaluates against current world state and is
//     therefore always coherent.
//
// CONTENT DRIFT
// Every reference is an id string, never an index, because a save outlives the
// files it was made against. Items and quests whose definitions have since
// disappeared are kept in the blob and reported (QuestLog::orphans,
// RpgSaveData::unknownItems) rather than silently dropped -- deleting a
// player's inventory because a designer renamed a row is not an acceptable
// failure mode.
namespace game::rpg {

struct ItemStackSnapshot {
    std::string item;
    int32_t count = 1;
    bool foundThisRun = false;
    float condition = 1.0f;
    // The seal the player spent against the next death (see LossPolicy.h). A
    // mid-expedition save that forgot this would quietly un-protect everything
    // the player had already decided to protect.
    bool secured = false;
};

// One trader's mutable half: the purse, the shelf, and the trade history the
// pricing curve reads. Definitions are not saved -- a rebalanced trader takes
// effect on the next load, the same rule the equipment modifiers follow.
struct TraderSnapshot {
    std::string id;
    int32_t purse = 0;
    int32_t daysSinceRestock = 0;
    std::vector<std::pair<std::string, int32_t>> flow;
    std::vector<std::pair<std::string, int32_t>> stock;
    std::vector<std::string> completedBarters;
};

struct QuestSnapshot {
    std::string id;
    uint8_t state = 0; // QuestState
    std::vector<int32_t> counts;
};

struct RpgSaveData {
    // Character. Experience per skill (OSRS) plus the accumulated character
    // experience (Tarkov); both levels are derived from these on load rather
    // than stored, so retuning either table retunes every existing save
    // instead of leaving old profiles on the old curve.
    std::vector<std::pair<std::string, int64_t>> skills;
    int64_t characterXp = 0;

    // Carrying.
    std::vector<ItemStackSnapshot> backpack;
    std::vector<ItemStackSnapshot> stash;
    // Indexed by EquipSlot; empty string means the slot is free. Stored
    // positionally because EquipSlot is append-only and the vector's length is
    // written, so a save from before a slot existed reads short and the new
    // slot comes back empty.
    std::vector<std::string> equipped;
    int32_t currency = 0;

    // Progress.
    std::vector<QuestSnapshot> quests;
    std::vector<std::string> flags;
    std::vector<std::pair<std::string, int32_t>> counters;
    std::vector<std::pair<std::string, int32_t>> standings;
    int32_t day = 0;
    int32_t deepestDepth = 0;

    // Economy.
    std::vector<TraderSnapshot> traders;

    // The safehouse and the village: station id -> built level.
    std::vector<std::pair<std::string, int32_t>> stations;
};

namespace rpgsave {

// Bump when the record layout changes.
inline constexpr uint16_t kVersion = 2;

// --- the pure half ---------------------------------------------------------

std::vector<uint8_t> encode(const RpgSaveData&);
std::optional<RpgSaveData> decode(const uint8_t* data, std::size_t size);
inline std::optional<RpgSaveData> decode(const std::vector<uint8_t>& bytes)
{
    return decode(bytes.data(), bytes.size());
}

bool writeFile(const std::string& path, const RpgSaveData&);
std::optional<RpgSaveData> readFile(const std::string& path);

// --- the half that needs the live objects ----------------------------------

RpgSaveData capture(const SkillSet&, const Inventory&, const QuestBook&,
                    const WorldState&, const Market&, const Hideout&);

// Rebuild the live quest objects from a snapshot. Needs the library (to know
// which subclass each id is) and the channels (so a restored Active quest
// re-subscribes). Quests whose rows have disappeared are reported, not dropped
// silently.
std::vector<std::string> restoreQuests(const RpgSaveData&, const QuestLibrary&,
                                       QuestBook&);

// Overwrite the live objects from `data`. `library` is consulted only to report
// items the content no longer defines; nothing is dropped on its say-so. The
// equipment modifier layer is rebuilt here, which is why the sheet comes back
// consistent without the save carrying it.
//
// Returns the item ids the library could not resolve, for the log line that
// tells a developer their save predates a rename.
std::vector<std::string> restore(const RpgSaveData& data,
                                 const ItemLibrary& items,
                                 const QuestLibrary& quests,
                                 const TraderLibrary& traders, SkillSet&,
                                 CharacterSheet&, Inventory&, QuestBook&,
                                 WorldState&, Market&, Hideout&);

// Rebuild every equipment modifier group on the sheet from what is worn.
// Called by restore, and by anything that changes equipment: it is idempotent,
// which is what makes "just re-apply everything" the correct response to any
// inventory change.
void syncEquipmentModifiers(const ItemLibrary&, const Equipment&,
                            CharacterSheet&);

} // namespace rpgsave

} // namespace game::rpg
