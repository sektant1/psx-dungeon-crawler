#pragma once
#include "RpgTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Progression, in two independent halves.
//
// OSRS-STYLE SKILLS
// A skill is a named track with its own experience and its own level, 1..99,
// raised by *doing the thing*. There is no point allocation and no build
// planning screen: you swing an axe, Attack goes up. Content is gated on
// individual skill levels ("you need 40 Lockpicking for that door"), and the
// sum of every skill is the total level a player quotes at each other.
//
// The experience table is OSRS's own, and it is not arbitrary. The requirement
// to reach level L is
//
//     xp(L) = floor( (1/4) * sum(n=1..L-1) floor(n + 300 * 2^(n/7)) )
//
// which gives 83 xp for level 2, 13,034,431 for level 99, and the property the
// whole game is built on: every ~7 levels costs roughly twice as much as the
// last. It is reproduced exactly rather than approximated, because an
// approximation would look right at level 20 and be tens of thousands of points
// out at level 80.
//
// TARKOV-STYLE CHARACTER LEVEL
// Separately, everything the player earns anywhere also feeds one *character*
// level. That is the number traders, quests and regions gate on, and it moves
// on a much shorter, steeper table than the skills do -- so a player who has
// levelled one skill to 60 is not automatically welcome everywhere, and a
// player who has done a bit of everything is.
//
// The two are deliberately not derived from each other. Skills say what you can
// *do*; character level says how far the world has opened up.
namespace game::rpg {

// One authored skill. Skills are data (progression.toml) rather than an enum,
// because "what can this character get better at" is a design decision that
// should not be a rebuild -- and because the derived-stat contributions below
// are how a skill stops being a number and starts mattering.
struct SkillDef {
    std::string id;
    std::string name;
    std::string description;
    // What each level of this skill contributes to the derived block. A skill
    // with no contributions is still legitimate: Lockpicking gates a door and
    // changes no stat at all.
    std::vector<StatModifier> perLevel;
    // Shown in the skills panel in this order; ties fall back to id.
    int sortOrder = 0;
};

// The whole authored skill list.
class SkillTable {
public:
    // Replaces the contents. Reads `[[skill]]` rows out of an already-parsed
    // TOML table (passed as void* for the reason ItemLibrary::parse documents).
    // This is the entry point progression.toml comes in through, since the
    // skill list shares that file with the curve and the starting kit.
    bool parse(const void* tomlTable);
    // Same, from a document of its own. For tests and for a future split of
    // the skill list into its own file.
    bool loadFromString(const char* tomlSrc);

    const SkillDef* find(const std::string& id) const;
    const std::vector<SkillDef>& all() const { return mSkills; }
    int size() const { return int(mSkills.size()); }
    // Index of a skill, or -1. Progress is stored by id, not by index, so this
    // is for UI ordering only.
    int indexOf(const std::string& id) const;

private:
    std::vector<SkillDef> mSkills;
};

namespace xp {

// The OSRS experience table. `levelForXp` is the inverse of `xpForLevel` and
// both are exact; the table is built once and shared.
inline constexpr int kMaxSkillLevel = 99;

int64_t xpForLevel(int level);       // total xp needed to *be* this level
int levelForXp(int64_t experience);  // clamped to [1, kMaxSkillLevel]
// What is left to the next level, and how far through it the player is.
int64_t xpToNextLevel(int64_t experience);
float levelProgress(int64_t experience); // 0..1

// The Tarkov-style character level table. Steeper and much shorter: reaching
// the cap is a campaign, not a career.
inline constexpr int kMaxCharacterLevel = 60;
int64_t xpForCharacterLevel(int level);
int characterLevelForXp(int64_t experience);

} // namespace xp

// A character's progression state: experience per skill, plus the accumulated
// total that drives the character level.
class SkillSet {
public:
    void setTable(const SkillTable* table) { mTable = table; }
    const SkillTable* table() const { return mTable; }

    int64_t experience(const std::string& skill) const;
    int level(const std::string& skill) const;
    // Sum of every skill's level. The number a player quotes.
    int totalLevel() const;
    int64_t totalExperience() const;

    // Award experience for doing something. Returns how many levels that
    // bought, so the caller fires one notification per level rather than one
    // per swing. Unknown skill ids are logged once and ignored -- a typo in a
    // weapon's `trains = "..."` should be visible, not silently discarded.
    int award(const std::string& skill, int64_t amount);

    // Character level, from `characterExperience`, which accumulates
    // everything: skill xp, quest rewards, extraction bonuses.
    int characterLevel() const { return xp::characterLevelForXp(mCharacterXp); }
    int64_t characterExperience() const { return mCharacterXp; }
    // Award character experience without touching a skill. For the rewards
    // that are not "you got better at something" -- surviving, extracting,
    // finishing a quest.
    int awardCharacter(int64_t amount);

    const std::unordered_map<std::string, int64_t>& raw() const { return mXp; }
    void setRaw(std::unordered_map<std::string, int64_t> xpById,
                int64_t characterXp);
    void clear();

    // Every skill's contribution to the derived block, as one modifier list.
    // Handed to CharacterSheet as the "skills" source, so skills and equipment
    // go through exactly the same layer and cannot double-count.
    std::vector<StatModifier> modifiers() const;

private:
    const SkillTable* mTable = nullptr;
    std::unordered_map<std::string, int64_t> mXp;
    int64_t mCharacterXp = 0;
};

} // namespace game::rpg
