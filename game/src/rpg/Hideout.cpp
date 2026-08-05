#include "Hideout.h"

#include "Quests.h" // readConditionArray

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>

namespace game::rpg {

const StationTier* StationDef::tier(int level) const
{
    if (level < 1 || level > int(tiers.size()))
        return nullptr;
    return &tiers[std::size_t(level - 1)];
}

bool StationLibrary::parse(const void* tomlTable)
{
    const toml::table& root = *static_cast<const toml::table*>(tomlTable);
    const toml::table* group = root["station"].as_table();
    if (!group) {
        eng::log::error("StationLibrary: document defines no [station.*] rows");
        return false;
    }

    mStations.clear();
    for (auto&& [key, node] : *group) {
        const toml::table* t = node.as_table();
        if (!t)
            continue;
        StationDef station;
        station.id = std::string(key.str());
        station.name = (*t)["name"].value_or(station.id);
        station.description = (*t)["description"].value_or(std::string());
        station.owner = (*t)["owner"].value_or(station.owner);

        if (const toml::array* tiers = (*t)["tier"].as_array()) {
            for (const toml::node& tn : *tiers) {
                const toml::table* tt = tn.as_table();
                if (!tt)
                    continue;
                StationTier tier;
                // The level is the position in the array, not an authored
                // field: an authored level can disagree with its position, and
                // there is no sensible thing to do when it does.
                tier.level = int(station.tiers.size()) + 1;
                tier.name = (*tt)["name"].value_or(
                    station.name + " " + std::to_string(tier.level));
                tier.description = (*tt)["description"].value_or(std::string());
                tier.currency = (*tt)["currency"].value_or(0);
                tier.stashSlots = (*tt)["stash_slots"].value_or(0);
                tier.carryBonus = float((*tt)["carry_bonus"].value_or(0.0));
                tier.requirements =
                    readConditionArray(tt, "requirement", station.id);

                if (const toml::array* mats = (*tt)["material"].as_array()) {
                    for (const toml::node& m : *mats) {
                        const toml::table* mt = m.as_table();
                        if (!mt)
                            continue;
                        const std::string id = (*mt)["item"].value_or(std::string());
                        if (!id.empty())
                            tier.materials.emplace_back(
                                id, std::max(1, (*mt)["count"].value_or(1)));
                    }
                }
                if (const toml::array* mods = (*tt)["modifier"].as_array()) {
                    for (const toml::node& m : *mods) {
                        const toml::table* mt = m.as_table();
                        if (!mt)
                            continue;
                        StatModifier mod;
                        const std::string field =
                            (*mt)["stat"].value_or(std::string());
                        if (!parseStatField(field, mod.field)) {
                            eng::log::error("stations: '%s' tier %d modifies "
                                            "'%s', which is not a stat",
                                            station.id.c_str(), tier.level,
                                            field.c_str());
                            continue;
                        }
                        mod.flat = float((*mt)["flat"].value_or(0.0));
                        mod.percent = float((*mt)["percent"].value_or(0.0));
                        tier.modifiers.push_back(mod);
                    }
                }
                if (const toml::array* flags = (*tt)["grants_flags"].as_array())
                    for (const toml::node& f : *flags)
                        if (const std::optional<std::string> s =
                                f.value<std::string>())
                            tier.grantsFlags.push_back(*s);

                station.tiers.push_back(std::move(tier));
            }
        }
        if (station.tiers.empty()) {
            eng::log::error("StationLibrary: '%s' has no tiers; dropped",
                            station.id.c_str());
            continue;
        }
        mStations[station.id] = std::move(station);
    }

    eng::log::info("StationLibrary: %d stations", int(mStations.size()));
    return !mStations.empty();
}

bool StationLibrary::load(const std::string& tomlPath)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("StationLibrary: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        mStations.clear();
        return false;
    }
    return parse(&parsed.table());
}

bool StationLibrary::loadFromString(const char* tomlSrc)
{
    toml::parse_result parsed = toml::parse(tomlSrc);
    if (!parsed) {
        eng::log::error("StationLibrary: %s",
                        std::string(parsed.error().description()).c_str());
        mStations.clear();
        return false;
    }
    return parse(&parsed.table());
}

const StationDef* StationLibrary::find(const std::string& id) const
{
    const auto it = mStations.find(id);
    return it == mStations.end() ? nullptr : &it->second;
}

std::vector<std::string> StationLibrary::ids() const
{
    std::vector<std::string> out;
    out.reserve(mStations.size());
    for (const auto& [id, s] : mStations)
        out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> StationLibrary::ids(const std::string& owner) const
{
    std::vector<std::string> out;
    for (const auto& [id, s] : mStations)
        if (s.owner == owner)
            out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> StationLibrary::referencedItems() const
{
    std::vector<std::string> out;
    for (const auto& [id, s] : mStations)
        for (const StationTier& t : s.tiers)
            for (const auto& [item, count] : t.materials)
                out.push_back(item);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// Hideout
// ---------------------------------------------------------------------------

int Hideout::level(const std::string& station) const
{
    const auto it = mLevels.find(station);
    return it == mLevels.end() ? 0 : it->second;
}

void Hideout::setLevel(const std::string& station, int level)
{
    if (level <= 0)
        mLevels.erase(station);
    else
        mLevels[station] = level;
}

const StationTier* Hideout::next(const StationLibrary& library,
                                 const std::string& station) const
{
    const StationDef* def = library.find(station);
    if (!def)
        return nullptr;
    return def->tier(level(station) + 1);
}

std::vector<StatModifier> Hideout::modifiers(const StationLibrary& library) const
{
    std::vector<StatModifier> out;
    for (const auto& [id, level] : mLevels) {
        const StationDef* def = library.find(id);
        if (!def)
            continue;
        // Every tier up to and including the built one contributes. Tiers are
        // cumulative rather than replacing, so an author writes what each
        // upgrade *adds* instead of restating the whole station every time.
        for (int l = 1; l <= level; ++l)
            if (const StationTier* tier = def->tier(l))
                out.insert(out.end(), tier->modifiers.begin(),
                           tier->modifiers.end());
    }
    return out;
}

int Hideout::stashSlots(const StationLibrary& library) const
{
    int slots = 0;
    for (const auto& [id, level] : mLevels) {
        const StationDef* def = library.find(id);
        if (!def)
            continue;
        for (int l = 1; l <= level; ++l)
            if (const StationTier* tier = def->tier(l))
                slots += tier->stashSlots;
    }
    return slots;
}

float Hideout::carryBonus(const StationLibrary& library) const
{
    float bonus = 0.0f;
    for (const auto& [id, level] : mLevels) {
        const StationDef* def = library.find(id);
        if (!def)
            continue;
        for (int l = 1; l <= level; ++l)
            if (const StationTier* tier = def->tier(l))
                bonus += tier->carryBonus;
    }
    return bonus;
}

} // namespace game::rpg
