#include "Items.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace game::rpg {

bool ItemDef::hasTag(std::string_view t) const
{
    return std::find(tags.begin(), tags.end(), t) != tags.end();
}

namespace {

float num(const toml::table& t, const char* key, float fallback)
{
    return float(t[key].value_or(double(fallback)));
}

// `[[item.<id>.modifier]]` -- one array entry per stat term. Replaced wholesale
// on override rather than merged, because "this item's bonuses" is a list an
// author reads as a unit; a merged list would make removing an inherited
// modifier impossible without a delete syntax nothing else in the content
// tree has.
void readModifiers(const toml::table& t, const char* key, const char* what,
                   const std::string& owner, std::vector<StatModifier>& out)
{
    const toml::array* arr = t[key].as_array();
    if (!arr)
        return;
    out.clear();
    for (const toml::node& node : *arr) {
        const toml::table* m = node.as_table();
        if (!m)
            continue;
        StatModifier mod;
        const std::string field = (*m)["stat"].value_or(std::string());
        if (!parseStatField(field, mod.field)) {
            eng::log::error("items: %s '%s' modifies '%s', which is not a stat",
                            what, owner.c_str(), field.c_str());
            continue;
        }
        mod.flat = num(*m, "flat", 0.0f);
        mod.percent = num(*m, "percent", 0.0f);
        out.push_back(mod);
    }
}

void readUse(const toml::table& t, const std::string& owner, UseDef& use)
{
    const std::string kind = t["kind"].value_or(std::string("none"));
    static constexpr const char* kNames[] = {
        "none", "restore_health", "restore_stamina", "restore_mana",
        "cure_status", "grant_effect"};
    bool matched = false;
    for (std::size_t i = 0; i < std::size(kNames); ++i) {
        if (kind == kNames[i]) {
            use.kind = UseKind(i);
            matched = true;
            break;
        }
    }
    if (!matched)
        eng::log::error("items: item '%s' has use kind '%s', which is not one "
                        "of none/restore_health/restore_stamina/restore_mana/"
                        "cure_status/grant_effect",
                        owner.c_str(), kind.c_str());
    use.magnitude = num(t, "magnitude", use.magnitude);
    use.duration = num(t, "duration", use.duration);
    use.consumesStack = t["consumes"].value_or(use.consumesStack);
    readModifiers(t, "grant", "item", owner, use.grants);
}

void readResistances(const toml::table& t, std::vector<ItemDef::ResistanceGrant>& out)
{
    out.clear();
    for (auto&& [key, node] : t) {
        ItemDef::ResistanceGrant g;
        g.channel = std::string(key.str());
        g.amount = float(node.value_or(0.0));
        out.push_back(std::move(g));
    }
}

void readTags(const toml::table& t, std::vector<std::string>& out)
{
    const toml::array* arr = t["tags"].as_array();
    if (!arr)
        return;
    out.clear();
    for (const toml::node& node : *arr)
        if (const std::optional<std::string> s = node.value<std::string>())
            out.push_back(*s);
}

// Overlay one authored table onto an already-inherited def.
void readInto(const toml::table& t, ItemDef& def)
{
    def.name = t["name"].value_or(def.name);
    def.description = t["description"].value_or(def.description);
    if (const std::optional<std::string> c = t["category"].value<std::string>()) {
        if (!parseItemCategory(*c, def.category))
            eng::log::error("items: item '%s' has category '%s', which is not "
                            "one of junk/reagent/consumable/weapon/armour/"
                            "trinket/relic/key/currency",
                            def.id.c_str(), c->c_str());
    }
    if (const std::optional<std::string> r = t["rarity"].value<std::string>()) {
        if (!parseRarity(*r, def.rarity))
            eng::log::error("items: item '%s' has rarity '%s'", def.id.c_str(),
                            r->c_str());
    }
    if (const std::optional<std::string> s = t["slot"].value<std::string>()) {
        if (!parseEquipSlot(*s, def.slot))
            eng::log::error("items: item '%s' equips to '%s', which is not a "
                            "slot", def.id.c_str(), s->c_str());
    }
    def.stackMax = t["stack"].value_or(def.stackMax);
    def.weight = num(t, "weight", def.weight);
    if (const toml::array* size = t["size"].as_array(); size && size->size() == 2) {
        def.gridWidth = std::max(1, (*size)[0].value_or(def.gridWidth));
        def.gridHeight = std::max(1, (*size)[1].value_or(def.gridHeight));
    }
    def.value = t["value"].value_or(def.value);
    def.demandElasticity = num(t, "elasticity", def.demandElasticity);
    def.tradeable = t["tradeable"].value_or(def.tradeable);
    def.durabilityMax = num(t, "durability", def.durabilityMax);
    def.wornValueFloor = num(t, "worn_value_floor", def.wornValueFloor);
    def.questItem = t["quest_item"].value_or(def.questItem);
    def.dropOnDeath = t["drop_on_death"].value_or(def.dropOnDeath);
    def.weaponId = t["weapon"].value_or(def.weaponId);
    def.event = t["event"].value_or(def.event);
    def.mesh = t["mesh"].value_or(def.mesh);
    def.material = t["material"].value_or(def.material);
    def.pickupScale = num(t, "pickup_scale", def.pickupScale);
    def.pickupEffect = t["pickup_effect"].value_or(def.pickupEffect);
    readModifiers(t, "modifier", "item", def.id, def.modifiers);
    readTags(t, def.tags);
    if (const toml::table* r = t["resistance"].as_table())
        readResistances(*r, def.resistances);
    if (const toml::table* u = t["use"].as_table())
        readUse(*u, def.id, def.use);
}

struct StagedRow {
    const toml::table* table = nullptr;
    std::string inherits;
    bool real = false; // an [item.*] row; archetypes never exist in the world
};

} // namespace

bool ItemLibrary::parse(const void* tomlTable)
{
    const toml::table& root = *static_cast<const toml::table*>(tomlTable);

    std::unordered_map<std::string, StagedRow> staged;
    const auto stage = [&](const char* section, bool real) {
        const toml::table* group = root[section].as_table();
        if (!group)
            return;
        for (auto&& [key, node] : *group) {
            const toml::table* t = node.as_table();
            if (!t)
                continue;
            const std::string id(key.str());
            if (staged.count(id)) {
                eng::log::error("ItemLibrary: duplicate id '%s'", id.c_str());
                continue;
            }
            staged[id] = {t, (*t)["inherits"].value_or(std::string()), real};
        }
    };
    stage("archetype", false);
    stage("item", true);

    if (staged.empty()) {
        eng::log::error("ItemLibrary: document defines no items");
        return false;
    }

    std::unordered_map<std::string, ItemDef> flat;
    std::unordered_set<std::string> inProgress;
    std::function<const ItemDef*(const std::string&)> flatten =
        [&](const std::string& id) -> const ItemDef* {
        if (auto it = flat.find(id); it != flat.end())
            return &it->second;
        const auto row = staged.find(id);
        if (row == staged.end())
            return nullptr;
        if (!inProgress.insert(id).second) {
            eng::log::error("ItemLibrary: '%s' inherits itself (cycle)",
                            id.c_str());
            return nullptr;
        }
        ItemDef def;
        if (!row->second.inherits.empty()) {
            const ItemDef* base = flatten(row->second.inherits);
            if (!base) {
                eng::log::error("ItemLibrary: '%s' inherits '%s', which does "
                                "not exist; row dropped",
                                id.c_str(), row->second.inherits.c_str());
                inProgress.erase(id);
                return nullptr;
            }
            def = *base;
        }
        def.id = id;
        readInto(*row->second.table, def);
        inProgress.erase(id);
        return &(flat[id] = std::move(def));
    };

    mItems.clear();
    for (const auto& [id, row] : staged) {
        const ItemDef* def = flatten(id);
        if (!def || !row.real)
            continue;
        // A stack size below one is the kind of typo that turns "add 3 nails"
        // into an infinite loop in the container. Reject the row instead.
        if (def->stackMax < 1) {
            eng::log::error("ItemLibrary: item '%s' has stack = %d; must be at "
                            "least 1. Row dropped.",
                            id.c_str(), def->stackMax);
            continue;
        }
        if (def->category == ItemCategory::Weapon && def->weaponId.empty())
            eng::log::error("ItemLibrary: item '%s' is category weapon but "
                            "names no `weapon = \"<id>\"` row in weapons.toml; "
                            "equipping it will do nothing",
                            id.c_str());
        mItems[id] = std::make_shared<ItemDef>(*def);
    }

    eng::log::info("ItemLibrary: %d items from %d rows", int(mItems.size()),
                   int(staged.size()));
    return !mItems.empty();
}

bool ItemLibrary::load(const std::string& tomlPath)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("ItemLibrary: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        mItems.clear();
        return false;
    }
    if (!parse(&parsed.table()))
        return false;
    mSourcePath = tomlPath;
    return true;
}

bool ItemLibrary::loadFromString(const char* tomlSrc)
{
    toml::parse_result parsed = toml::parse(tomlSrc);
    if (!parsed) {
        eng::log::error("ItemLibrary: %s",
                        std::string(parsed.error().description()).c_str());
        mItems.clear();
        return false;
    }
    mSourcePath.clear();
    return parse(&parsed.table());
}

void ItemLibrary::resolve(const CombatVocabulary& vocabulary)
{
    for (auto& [id, defPtr] : mItems) {
        for (ItemDef::ResistanceGrant& g : defPtr->resistances) {
            g.id = vocabulary.damageType(g.channel);
            if (g.id == kInvalidDamageType)
                eng::log::error("ItemLibrary: item '%s' resists '%s', which "
                                "magic.toml does not define; ignored",
                                id.c_str(), g.channel.c_str());
        }
    }
}

ItemLibrary::Ref ItemLibrary::find(const std::string& id) const
{
    const auto it = mItems.find(id);
    return it == mItems.end() ? Ref{} : it->second;
}

ItemDef* ItemLibrary::mutableDef(const std::string& id)
{
    const auto it = mItems.find(id);
    return it == mItems.end() ? nullptr : it->second.get();
}

std::vector<std::string> ItemLibrary::ids() const
{
    std::vector<std::string> out;
    out.reserve(mItems.size());
    for (const auto& [id, def] : mItems)
        out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// Loot
// ---------------------------------------------------------------------------

bool LootLibrary::parse(const void* tomlTable)
{
    const toml::table& root = *static_cast<const toml::table*>(tomlTable);
    const toml::table* group = root["table"].as_table();
    if (!group) {
        eng::log::error("LootLibrary: document defines no [table.*] rows");
        return false;
    }

    mTables.clear();
    for (auto&& [key, node] : *group) {
        const toml::table* t = node.as_table();
        if (!t)
            continue;
        LootTable table;
        table.id = std::string(key.str());
        table.rolls = (*t)["rolls"].value_or(table.rolls);
        table.coinMin = (*t)["coin_min"].value_or(table.coinMin);
        table.coinMax = (*t)["coin_max"].value_or(table.coinMax);
        if (table.coinMax < table.coinMin)
            table.coinMax = table.coinMin;

        if (const toml::array* entries = (*t)["entry"].as_array()) {
            for (const toml::node& e : *entries) {
                const toml::table* et = e.as_table();
                if (!et)
                    continue;
                LootEntry entry;
                entry.item = (*et)["item"].value_or(std::string());
                if (entry.item.empty()) {
                    eng::log::error("LootLibrary: table '%s' has an entry with "
                                    "no item", table.id.c_str());
                    continue;
                }
                entry.weight = std::max(0, (*et)["weight"].value_or(entry.weight));
                entry.minCount = std::max(1, (*et)["min"].value_or(entry.minCount));
                entry.maxCount = std::max(entry.minCount,
                                          (*et)["max"].value_or(entry.maxCount));
                entry.chance = float((*et)["chance"].value_or(double(entry.chance)));
                entry.guaranteed = (*et)["guaranteed"].value_or(entry.guaranteed);
                table.entries.push_back(std::move(entry));
            }
        }
        if (table.entries.empty() && table.coinMax <= 0)
            eng::log::error("LootLibrary: table '%s' drops nothing at all",
                            table.id.c_str());
        mTables[table.id] = std::move(table);
    }

    eng::log::info("LootLibrary: %d drop tables", int(mTables.size()));
    return !mTables.empty();
}

bool LootLibrary::load(const std::string& tomlPath)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("LootLibrary: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        mTables.clear();
        return false;
    }
    return parse(&parsed.table());
}

bool LootLibrary::loadFromString(const char* tomlSrc)
{
    toml::parse_result parsed = toml::parse(tomlSrc);
    if (!parsed) {
        eng::log::error("LootLibrary: %s",
                        std::string(parsed.error().description()).c_str());
        mTables.clear();
        return false;
    }
    return parse(&parsed.table());
}

const LootTable* LootLibrary::find(const std::string& id) const
{
    const auto it = mTables.find(id);
    return it == mTables.end() ? nullptr : &it->second;
}

std::vector<std::string> LootLibrary::ids() const
{
    std::vector<std::string> out;
    out.reserve(mTables.size());
    for (const auto& [id, t] : mTables)
        out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> LootLibrary::referencedItems() const
{
    std::vector<std::string> out;
    for (const auto& [id, table] : mTables)
        for (const LootEntry& e : table.entries)
            out.push_back(e.item);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

namespace loot {

uint32_t nextRandom(uint32_t& state)
{
    // xorshift32. Zero is a fixed point, so a caller that seeded with 0 gets a
    // usable stream instead of an endless run of zeroes.
    if (state == 0)
        state = 0x9E3779B9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float nextFloat(uint32_t& state)
{
    return float(nextRandom(state) >> 8) / float(1 << 24);
}

LootResult roll(const LootTable& table, uint32_t& rng, float fortune)
{
    LootResult result;

    // Fortune widens the chance gates and nothing else. Capped, because a
    // late-game Fortune build turning every 2% relic into a certainty is how a
    // loot economy stops meaning anything.
    const float luck = std::clamp(1.0f + fortune * 0.02f, 1.0f, 2.5f);

    const auto emit = [&](const LootEntry& e) {
        const int span = e.maxCount - e.minCount + 1;
        const int count = e.minCount + int(nextRandom(rng) % uint32_t(span));
        // Merge with an existing drop of the same item so a table that rolls
        // "3 nails" twice reports one drop of six rather than two pickups
        // sitting inside each other.
        for (LootDrop& d : result.drops) {
            if (d.item == e.item) {
                d.count += count;
                return;
            }
        }
        result.drops.push_back({e.item, count});
    };

    // Guaranteed entries first, outside the weighted pool: a quest item that
    // must drop should not be competing with three commons for the same roll.
    int totalWeight = 0;
    for (const LootEntry& e : table.entries) {
        if (e.guaranteed) {
            if (nextFloat(rng) < e.chance * luck)
                emit(e);
        } else {
            totalWeight += e.weight;
        }
    }

    for (int r = 0; r < table.rolls && totalWeight > 0; ++r) {
        int pick = int(nextRandom(rng) % uint32_t(totalWeight));
        for (const LootEntry& e : table.entries) {
            if (e.guaranteed)
                continue;
            pick -= e.weight;
            if (pick >= 0)
                continue;
            // The weighted pick chose the entry; its own chance still gates it,
            // so a table can hold a rare beside commons without the rare
            // stealing a whole roll every time it is picked.
            if (nextFloat(rng) < e.chance * luck)
                emit(e);
            break;
        }
    }

    if (table.coinMax > 0) {
        const int span = table.coinMax - table.coinMin + 1;
        result.coin = table.coinMin + int(nextRandom(rng) % uint32_t(span));
    }
    return result;
}

} // namespace loot

} // namespace game::rpg
