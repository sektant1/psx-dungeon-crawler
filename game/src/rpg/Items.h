#pragma once
#include "RpgTypes.h"
#include "combat/CombatVocabulary.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// What things are, and what falls out of what.
//
// Two tables and two loaders, in one module because they are one authoring
// question: a loot row is meaningless without the item rows it names, and the
// library validates that link at load rather than at the moment a corpse hits
// the floor.
namespace game::rpg {

// What using an item does. Deliberately a small closed set: a consumable that
// needs behaviour beyond this is a scripted item, and the seam for that is
// `event`, which publishes into the same typed stream quests read.
enum class UseKind {
    None,
    RestoreHealth,
    RestoreStamina,
    RestoreMana,
    CureStatus,   // clears every active crowd-control effect
    GrantEffect,  // a timed modifier group (see UseDef::duration)
    Count
};

struct UseDef {
    UseKind kind = UseKind::None;
    float magnitude = 0.0f;
    float duration = 0.0f;             // GrantEffect only
    std::vector<StatModifier> grants;  // GrantEffect only
    bool consumesStack = true;
};

// One authored item.
//
// The economy fields are three separate numbers on purpose (AGENTS.md §15:
// "the economy should create conflicting values"). `value` is what a generic
// vendor pays; a villager who *needs* the thing pays their own price, which
// lives on the quest or the NPC, not here.
struct ItemDef {
    std::string id;
    std::string name = "Unnamed";
    std::string description;
    ItemCategory category = ItemCategory::Junk;
    Rarity rarity = Rarity::Common;

    int stackMax = 1;
    float weight = 0.5f; // kg-ish; carry capacity is in the same unit
    // Footprint in grid cells, for the containers running in grid mode. 1x1 is
    // the default so an item that never states dimensions still fits
    // somewhere; a rifle is 4x1 and a med case is 2x2.
    int gridWidth = 1;
    int gridHeight = 1;
    int value = 0;       // base market price in coin
    // Economy inputs (see Trading.h). `demandElasticity` is how hard the price
    // moves as the player floods or starves the market: 0 pins it to `value`,
    // which is right for currency and for anything with an official price.
    float demandElasticity = 0.35f;
    bool tradeable = true;
    // Wear. `durabilityMax <= 0` means the item does not wear at all, which is
    // everything that is not equipment.
    float durabilityMax = 0.0f;
    // Fraction of `value` a fully worn item is worth, so condition is a real
    // trading decision rather than a cosmetic bar.
    float wornValueFloor = 0.2f;
    bool questItem = false;   // never sold, never lost on death
    bool dropOnDeath = true;  // false for the few things a death cannot take

    // Equipment.
    EquipSlot slot = EquipSlot::None;
    std::vector<StatModifier> modifiers;
    // Resistance grants, authored by channel name and resolved to ids by
    // resolve(). Same append-only warning as EnemyDef: the row is only as
    // meaningful as the vocabulary that produced the ids.
    struct ResistanceGrant {
        std::string channel;
        float amount = 0.0f;
        DamageTypeId id = kInvalidDamageType;
    };
    std::vector<ResistanceGrant> resistances;

    // Weapon items hand off to the existing loadout rather than modelling a
    // second one: this is a row id in weapons.toml.
    std::string weaponId;

    UseDef use;
    // Published when the item is used, so a consumable can advance a quest
    // objective without the quest system knowing what a potion is.
    std::string event;

    // World presentation for the dropped/placed pickup. Empty mesh falls back
    // to the primitive, which is what every prototype item ships with.
    std::string mesh;
    std::string material;
    float pickupScale = 1.0f;
    std::string pickupEffect; // particle effect that idles on the pickup

    std::vector<std::string> tags; // "reagent.ash", "lodge", "medicine"

    bool stackable() const { return stackMax > 1; }
    bool hasTag(std::string_view t) const;
};

// The item table, loaded from assets/config/items.toml.
//
// Same two-kinds-of-row shape as EnemyLibrary: `[archetype.<id>]` rows are
// templates that never exist in the world, `[item.<id>]` rows do and may name
// an archetype (or another item) in `inherits`. Inheritance is field-wise and
// transitive.
class ItemLibrary {
public:
    bool load(const std::string& tomlPath);
    bool loadFromString(const char* tomlSrc);

    // Turn every authored resistance channel name into an id. Call after load.
    void resolve(const CombatVocabulary& vocabulary);

    // Shared for the same reason EnemyLibrary::Ref is: a hot-reload destroys
    // every definition in the file, and an inventory stack holds on to its
    // definition across frames.
    using Ref = std::shared_ptr<const ItemDef>;

    Ref find(const std::string& id) const;
    std::vector<std::string> ids() const;  // sorted, for debug UI and validation
    int size() const { return int(mItems.size()); }
    ItemDef* mutableDef(const std::string& id);
    const std::string& sourcePath() const { return mSourcePath; }

private:
    bool parse(const void* tomlTable);

    std::unordered_map<std::string, std::shared_ptr<ItemDef>> mItems;
    std::string mSourcePath;
};

// ---------------------------------------------------------------------------
// Loot
// ---------------------------------------------------------------------------

// One possible drop. `weight` is relative within its table; `chance` gates the
// entry independently, so a table can hold a 1-in-50 relic beside three
// always-rolled commons without distorting the weights.
struct LootEntry {
    std::string item;
    int weight = 1;
    int minCount = 1;
    int maxCount = 1;
    float chance = 1.0f;
    bool guaranteed = false; // rolled separately, before the weighted picks
};

struct LootTable {
    std::string id;
    int rolls = 1;
    std::vector<LootEntry> entries;
    // Coin, rolled independently of items. Most enemies drop a little; a table
    // that wants none authors 0.
    int coinMin = 0;
    int coinMax = 0;
};

// What one roll produced. Plain data so the caller decides whether it becomes a
// world pickup, an inventory add, or a line in a test.
struct LootDrop {
    std::string item;
    int count = 1;
};

struct LootResult {
    std::vector<LootDrop> drops;
    int coin = 0;
};

class LootLibrary {
public:
    bool load(const std::string& tomlPath);
    bool loadFromString(const char* tomlSrc);

    const LootTable* find(const std::string& id) const;
    std::vector<std::string> ids() const;
    int size() const { return int(mTables.size()); }

    // Every item id any table names, so a caller can fail the *load* on a
    // dangling reference rather than dropping nothing at runtime.
    std::vector<std::string> referencedItems() const;

private:
    bool parse(const void* tomlTable);
    std::unordered_map<std::string, LootTable> mTables;
};

namespace loot {

// Deterministic given the seed. The RNG is passed by reference and advanced, so
// a caller that wants a reproducible expedition threads one stream through
// every drop -- the same discipline EnemyAI's brain RNG follows.
//
// `fortune` scales the chance gates (a Fortune-heavy character sees the rare
// entries more often) but never the weights, so it cannot conjure an item the
// table does not hold.
LootResult roll(const LootTable& table, uint32_t& rng, float fortune = 0.0f);

// xorshift32, the same generator the enemy brain uses, so "seeded" means one
// thing across the game layer.
uint32_t nextRandom(uint32_t& state);
float nextFloat(uint32_t& state); // [0, 1)

} // namespace loot

} // namespace game::rpg
