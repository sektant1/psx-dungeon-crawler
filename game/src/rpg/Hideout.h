#pragma once
#include "RpgTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

// The safehouse, and the village around it.
//
// One mechanic, two owners. The player's house in the village is a set of
// **stations** that are built and then upgraded -- a workbench, a still, a
// strongbox, a map table -- and the village itself is the same thing at a
// larger scale: a repaired well, a reopened forge, a rebuilt gate. Both are
// "a project with a level, a cost, and something it unlocks", so both are
// `Station` rows and the only difference is who owns them.
//
// That is deliberate rather than lazy. AGENTS.md §16 wants each major NPC's
// progression to have "a visible effect on the village"; making a village
// upgrade the same object as a hideout upgrade means an NPC's questline can
// finish by unlocking one, and the player sees their own base and the street
// outside it improve through the same system.
//
// A station level does three things: it satisfies conditions (so content can
// gate on "strongbox 2"), it contributes stat modifiers (a better bed is
// health regen), and it grants capacity (the strongbox is what makes the stash
// bigger). Nothing here ticks -- upgrades complete when they are paid for, and
// time advances through expeditions, per §17.
namespace game::rpg {

// One level of one project.
struct StationTier {
    int level = 1;
    std::string name;
    std::string description;
    // What it costs to reach this tier.
    std::vector<std::pair<std::string, int>> materials; // item id, count
    int currency = 0;
    // And what else must be true. Reuses the shared Condition, so "requires
    // the blacksmith at standing 20" and "requires Smithing 30" are already
    // expressible.
    std::vector<Condition> requirements;

    // What having it does.
    std::vector<StatModifier> modifiers;
    // Extra stash slots and extra carry weight, the two capacities the base is
    // actually about.
    int stashSlots = 0;
    float carryBonus = 0.0f;
    // Flags set on completion, so quests and dialogue can react without the
    // hideout knowing they exist.
    std::vector<std::string> grantsFlags;
};

struct StationDef {
    std::string id;
    std::string name;
    std::string description;
    // "hideout" for the player's house, "village" for the street outside it.
    // Free text rather than an enum: the districts of a growing village are
    // content, and a new one should not be a rebuild.
    std::string owner = "hideout";
    std::vector<StationTier> tiers;

    const StationTier* tier(int level) const;
    int maxLevel() const { return int(tiers.size()); }
};

class StationLibrary {
public:
    bool load(const std::string& tomlPath);
    bool loadFromString(const char* tomlSrc);

    const StationDef* find(const std::string& id) const;
    std::vector<std::string> ids() const;
    std::vector<std::string> ids(const std::string& owner) const;
    int size() const { return int(mStations.size()); }
    std::vector<std::string> referencedItems() const;

private:
    bool parse(const void* tomlTable);
    std::unordered_map<std::string, StationDef> mStations;
};

// What has actually been built. Level 0 means "not built at all", which is why
// tiers are 1-based.
class Hideout {
public:
    int level(const std::string& station) const;
    void setLevel(const std::string& station, int level);
    const std::unordered_map<std::string, int>& levels() const { return mLevels; }
    void clear() { mLevels.clear(); }

    // The next tier, or null when the station is unknown or already maxed.
    const StationTier* next(const StationLibrary&,
                            const std::string& station) const;

    // Everything every built tier contributes, as one modifier list, ready for
    // CharacterSheet::setModifiers under `kHideoutModifierSource`.
    std::vector<StatModifier> modifiers(const StationLibrary&) const;
    int stashSlots(const StationLibrary&) const;
    float carryBonus(const StationLibrary&) const;

private:
    std::unordered_map<std::string, int> mLevels;
};

inline constexpr const char* kHideoutModifierSource = "hideout";

} // namespace game::rpg
