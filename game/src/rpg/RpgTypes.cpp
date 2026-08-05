#include "RpgTypes.h"

#include <array>

namespace game::rpg {

namespace {

// One table per enum, indexed by the enum value, asserted to be complete. The
// static_asserts are the point: adding a value to the enum and forgetting the
// spelling is a compile error rather than an item category that loads as Junk.
template <class E, std::size_t N>
bool parseFrom(const std::array<const char*, N>& names, std::string_view text,
               E& out)
{
    for (std::size_t i = 0; i < N; ++i) {
        if (text == names[i]) {
            out = E(i);
            return true;
        }
    }
    return false;
}

template <class E, std::size_t N>
const char* nameFrom(const std::array<const char*, N>& names, E value)
{
    const auto i = std::size_t(value);
    return i < N ? names[i] : "?";
}

constexpr std::array<const char*, std::size_t(ItemCategory::Count)>
    kCategories{"junk",   "reagent", "consumable", "weapon", "armour",
                "trinket", "relic",  "key",        "currency"};

constexpr std::array<const char*, std::size_t(EquipSlot::Count)> kSlots{
    "none", "head", "body", "hands", "cloak", "amulet", "ring", "sigil"};

constexpr std::array<const char*, std::size_t(Rarity::Count)> kRarities{
    "common", "uncommon", "rare", "arcane", "relic"};

constexpr std::array<const char*, std::size_t(StatField::Count)> kStatFields{
    "might",         "agility",       "vigour",      "attunement",
    "fortune",       "health_max",    "stamina_max", "mana_max",
    "poise_max",     "carry_capacity","melee_power", "cast_power",
    "crit_chance",   "move_speed",    "stamina_regen", "mana_regen"};

constexpr std::array<const char*, std::size_t(EventKind::Count)> kEvents{
    "enemy_killed", "item_acquired", "item_lost",  "item_used",
    "npc_talked",   "flag_set",      "depth_reached", "extracted",
    "player_died",  "level_gained",  "quest_completed"};

constexpr std::array<const char*, std::size_t(ConditionKind::Count)>
    kConditions{"always",          "flag_set",        "flag_clear",
                "has_item",        "quest_active",    "quest_completed",
                "quest_unstarted", "level_at_least",  "standing_at_least",
                "currency_at_least"};

constexpr std::array<const char*, std::size_t(EffectKind::Count)> kEffects{
    "none",          "set_flag",       "clear_flag",   "give_item",
    "take_item",     "give_currency",  "give_xp",      "start_quest",
    "complete_quest","turn_in_quest",  "fail_quest",   "add_standing",
    "advance_day"};

} // namespace

bool parseItemCategory(std::string_view t, ItemCategory& o)
{
    return parseFrom(kCategories, t, o);
}
bool parseEquipSlot(std::string_view t, EquipSlot& o)
{
    return parseFrom(kSlots, t, o);
}
bool parseRarity(std::string_view t, Rarity& o) { return parseFrom(kRarities, t, o); }
bool parseStatField(std::string_view t, StatField& o)
{
    return parseFrom(kStatFields, t, o);
}
bool parseEventKind(std::string_view t, EventKind& o)
{
    return parseFrom(kEvents, t, o);
}
bool parseConditionKind(std::string_view t, ConditionKind& o)
{
    return parseFrom(kConditions, t, o);
}
bool parseEffectKind(std::string_view t, EffectKind& o)
{
    return parseFrom(kEffects, t, o);
}

const char* nameOf(ItemCategory v) { return nameFrom(kCategories, v); }
const char* nameOf(EquipSlot v) { return nameFrom(kSlots, v); }
const char* nameOf(Rarity v) { return nameFrom(kRarities, v); }
const char* nameOf(StatField v) { return nameFrom(kStatFields, v); }
const char* nameOf(EventKind v) { return nameFrom(kEvents, v); }
const char* nameOf(ConditionKind v) { return nameFrom(kConditions, v); }
const char* nameOf(EffectKind v) { return nameFrom(kEffects, v); }

} // namespace game::rpg
