#pragma once
#include <eng/StringId.h>

#include <string>
#include <string_view>

// The RPG layer's shared vocabulary.
//
// Everything in game/src/rpg that more than one module has to agree about lives
// here: what an equipment slot is, what a stat field is called, what a quest
// objective is watching for, and the condition/effect pair that quests and
// dialogue both evaluate. One header, so "what words does the RPG content
// speak" has one answer a reader can find.
//
// Every enum here is paired with a name table in RpgTypes.cpp, because all of
// these arrive from TOML as text and go back out to a debug panel as text. The
// tables are the *only* place a spelling lives; a loader that wants to accept
// "main_hand" asks parseEquipSlot rather than writing its own if-chain.
namespace game::rpg {

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------

// What kind of thing an item is. This drives sorting, vendor interest and
// which systems will even look at a row -- not behaviour: a Consumable does
// nothing because it is a Consumable, it does something because it has a
// [item.<id>.use] table.
enum class ItemCategory {
    Junk,      // sells, does nothing else. The bulk of what a dungeon holds.
    Reagent,   // alchemy/crafting input. Recipes are not built yet; the
               // category exists so they have somewhere to land.
    Consumable,
    Weapon,    // names a row in weapons.toml; equipping swaps the loadout
    Armour,
    Trinket,
    Relic,     // lodge-relevant, high value, usually quest-bound
    Key,       // opens something; never sold, never dropped on death
    Currency,
    Count
};

// Where a piece of equipment goes. MainHand is deliberately *not* here:
// weapons are held by the existing PlayerWeapons loadout, and giving the RPG
// layer a competing hand slot would mean two systems both believing they own
// what the player is holding. An equippable weapon item names a weapon id and
// the loadout takes it from there.
enum class EquipSlot {
    None,
    Head,
    Body,
    Hands,
    Cloak,
    Amulet,
    Ring,
    Sigil, // the lodge's mark; the slot initiation unlocks
    Count
};

// How rare a row is meant to feel. Presentation only (tooltip colour, vendor
// patter); loot weight is authored per drop table, not derived from this.
enum class Rarity { Common, Uncommon, Rare, Arcane, Relic, Count };

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

// Every number a modifier is allowed to touch. Attributes come first so a
// derive() pass can apply them, recompute, and then apply the derived-field
// modifiers on top -- the order matters and the enum encodes it.
//
// APPEND ONLY. A saved modifier stores this as an integer, so reordering
// reinterprets every save.
enum class StatField {
    // Base attributes.
    Might,      // melee damage, carry capacity
    Agility,    // move speed, stamina, dodge cost
    Vigour,     // health, poise
    Attunement, // mana, cast power
    Fortune,    // crit chance, loot quality

    // Derived, for the items that want to state an outcome instead of an
    // attribute ("+15 health" rather than "+2 vigour").
    HealthMax,
    StaminaMax,
    ManaMax,
    PoiseMax,
    CarryCapacity,
    MeleePower,
    CastPower,
    CritChance,
    MoveSpeedScale,
    StaminaRegen,
    ManaRegen,

    Count
};

// True for the five attributes, which is what tells derive() where the second
// pass starts.
constexpr bool isAttribute(StatField f)
{
    return int(f) <= int(StatField::Fortune);
}

// One term in the modifier layer. `flat` is added, `percent` scales (0.15 =
// +15%); a modifier may carry both. Never applied to a base value in place --
// see stats::derive.
struct StatModifier {
    StatField field = StatField::Might;
    float flat = 0.0f;
    float percent = 0.0f;
};

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

// What the game tells the RPG layer happened. Typed, per AGENTS.md §28: a
// quest objective is three integers, and a typo in content is a load-time
// error rather than an objective that silently never fires.
//
// APPEND ONLY: quest saves store objective progress positionally, and content
// stores the kind by name, but a debug panel and the telemetry channel both
// print the integer.
enum class EventKind {
    EnemyKilled,   // subject = enemy definition id
    ItemAcquired,  // subject = item id, count = how many
    ItemLost,      // subject = item id (sold, used, given away, dropped)
    ItemUsed,      // subject = item id
    NpcTalked,     // subject = npc id
    FlagSet,       // subject = flag name
    DepthReached,  // subject = none, count = depth
    Extracted,     // subject = none, count = depth returned from
    PlayerDied,    // subject = killer's definition id when there was one
    LevelGained,   // subject = none, count = new level
    QuestCompleted,// subject = quest id
    Count
};

// One thing that happened. Deliberately tiny and copyable: the bus queues these
// by value and drains them once a frame.
struct GameEvent {
    EventKind kind = EventKind::EnemyKilled;
    eng::StringId subject;
    int count = 1;
};

// ---------------------------------------------------------------------------
// Conditions and effects
// ---------------------------------------------------------------------------

// A test against world state. Shared by quest prerequisites and dialogue
// choices, because they ask exactly the same questions and having two spellings
// of "player has 3 of X" is how content starts lying.
enum class ConditionKind {
    Always,
    FlagSet,
    FlagClear,
    HasItem,        // subject = item id, value = minimum count
    QuestActive,    // subject = quest id
    QuestCompleted, // subject = quest id (complete or turned in)
    QuestUnstarted, // subject = quest id
    LevelAtLeast,   // value = level
    StandingAtLeast,// subject = npc id, value = standing
    CurrencyAtLeast,// value = coin
    Count
};

struct Condition {
    ConditionKind kind = ConditionKind::Always;
    std::string subject; // kept as text: content ids, resolved at evaluation
    int value = 0;
    bool negate = false;
};

// A change to world state. Same argument as Condition: a dialogue choice and a
// quest reward hand out the same things.
enum class EffectKind {
    None,
    SetFlag,
    ClearFlag,
    GiveItem,     // subject = item id, value = count
    TakeItem,
    GiveCurrency, // value may be negative
    GiveXp,
    StartQuest,
    CompleteQuest, // force-complete, for the quests a conversation resolves
    TurnInQuest,
    FailQuest,
    AddStanding,  // subject = npc id, value = delta
    AdvanceDay,
    Count
};

struct Effect {
    EffectKind kind = EffectKind::None;
    std::string subject;
    int value = 0;
};

// ---------------------------------------------------------------------------
// Name tables
// ---------------------------------------------------------------------------
//
// parse* returns false and leaves the out-param untouched for an unknown name,
// so a loader can report the row and the spelling together. name* never fails:
// an out-of-range value prints "?" rather than indexing off the end.

bool parseItemCategory(std::string_view, ItemCategory&);
bool parseEquipSlot(std::string_view, EquipSlot&);
bool parseRarity(std::string_view, Rarity&);
bool parseStatField(std::string_view, StatField&);
bool parseEventKind(std::string_view, EventKind&);
bool parseConditionKind(std::string_view, ConditionKind&);
bool parseEffectKind(std::string_view, EffectKind&);

const char* nameOf(ItemCategory);
const char* nameOf(EquipSlot);
const char* nameOf(Rarity);
const char* nameOf(StatField);
const char* nameOf(EventKind);
const char* nameOf(ConditionKind);
const char* nameOf(EffectKind);

} // namespace game::rpg
