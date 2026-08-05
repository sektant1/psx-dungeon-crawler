#include "Skills.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace game::rpg {

namespace xp {

namespace {

// The OSRS table, built once. Index i holds the total experience required to
// be level i+1, so kTable[0] == 0 and kTable[98] == 13,034,431.
//
// The formula accumulates a running sum in a *separate* variable from the
// quarter-division: dividing each term by four as you go and summing the
// results gives a different (wrong) answer, because the real definition floors
// only once, at the end of the sum. That off-by-a-few-hundred error is the
// classic way this table gets reimplemented incorrectly.
const std::array<int64_t, kMaxSkillLevel>& table()
{
    static const std::array<int64_t, kMaxSkillLevel> kTable = [] {
        std::array<int64_t, kMaxSkillLevel> t{};
        double points = 0.0;
        t[0] = 0;
        for (int level = 1; level < kMaxSkillLevel; ++level) {
            points += std::floor(double(level) +
                                 300.0 * std::pow(2.0, double(level) / 7.0));
            t[std::size_t(level)] = int64_t(std::floor(points / 4.0));
        }
        return t;
    }();
    return kTable;
}

// The character-level table. Level 2 costs 1,000 and the level-60 cap costs
// about 4.37 million -- deliberately *less* than a single maxed skill's 13
// million, because the two numbers mean different things: the character level
// is campaign progress that a player is expected to finish, and a 99 is a
// career they are not.
//
// The exponent is 1.25 rather than the more obvious 1.65 for exactly that
// reason: at 1.65 the cap costs 18.6 million, so reaching character 60 would
// take longer than maxing a skill and the world would stay shut for most of
// the game. The RpgProgressionTests assertion that the character cap is
// cheaper than a maxed skill is what pins this down.
const std::array<int64_t, kMaxCharacterLevel>& characterTable()
{
    static const std::array<int64_t, kMaxCharacterLevel> kTable = [] {
        std::array<int64_t, kMaxCharacterLevel> t{};
        double total = 0.0;
        t[0] = 0;
        for (int level = 1; level < kMaxCharacterLevel; ++level) {
            // Cost of the level being bought, not the level being left.
            total += 1000.0 * std::pow(double(level), 1.25);
            t[std::size_t(level)] = int64_t(total);
        }
        return t;
    }();
    return kTable;
}

} // namespace

int64_t xpForLevel(int level)
{
    const int clamped = std::clamp(level, 1, kMaxSkillLevel);
    return table()[std::size_t(clamped - 1)];
}

int levelForXp(int64_t experience)
{
    const auto& t = table();
    // upper_bound then step back: the level is the last entry the player has
    // paid for.
    const auto it = std::upper_bound(t.begin(), t.end(), experience);
    const auto index = std::size_t(it - t.begin());
    return int(index == 0 ? 1 : index);
}

int64_t xpToNextLevel(int64_t experience)
{
    const int level = levelForXp(experience);
    if (level >= kMaxSkillLevel)
        return 0;
    return xpForLevel(level + 1) - experience;
}

float levelProgress(int64_t experience)
{
    const int level = levelForXp(experience);
    if (level >= kMaxSkillLevel)
        return 1.0f;
    const int64_t base = xpForLevel(level);
    const int64_t next = xpForLevel(level + 1);
    const int64_t span = next - base;
    if (span <= 0)
        return 1.0f;
    return float(double(experience - base) / double(span));
}

int64_t xpForCharacterLevel(int level)
{
    const int clamped = std::clamp(level, 1, kMaxCharacterLevel);
    return characterTable()[std::size_t(clamped - 1)];
}

int characterLevelForXp(int64_t experience)
{
    const auto& t = characterTable();
    const auto it = std::upper_bound(t.begin(), t.end(), experience);
    const auto index = std::size_t(it - t.begin());
    return int(index == 0 ? 1 : index);
}

} // namespace xp

// ---------------------------------------------------------------------------
// SkillTable
// ---------------------------------------------------------------------------

bool SkillTable::parse(const void* tomlTable)
{
    const toml::table& root = *static_cast<const toml::table*>(tomlTable);
    const toml::array* rows = root["skill"].as_array();
    if (!rows) {
        eng::log::error("progression.toml: no [[skill]] rows; the character "
                        "will have no skills at all");
        return false;
    }

    mSkills.clear();
    for (const toml::node& node : *rows) {
        const toml::table* t = node.as_table();
        if (!t)
            continue;
        SkillDef skill;
        skill.id = (*t)["id"].value_or(std::string());
        if (skill.id.empty()) {
            eng::log::error("progression.toml: a [[skill]] row has no id");
            continue;
        }
        if (find(skill.id)) {
            eng::log::error("progression.toml: duplicate skill '%s'",
                            skill.id.c_str());
            continue;
        }
        skill.name = (*t)["name"].value_or(skill.id);
        skill.description = (*t)["description"].value_or(std::string());
        skill.sortOrder = (*t)["order"].value_or(int(mSkills.size()));

        if (const toml::array* mods = (*t)["per_level"].as_array()) {
            for (const toml::node& m : *mods) {
                const toml::table* mt = m.as_table();
                if (!mt)
                    continue;
                StatModifier mod;
                const std::string field = (*mt)["stat"].value_or(std::string());
                if (!parseStatField(field, mod.field)) {
                    eng::log::error("progression.toml: skill '%s' contributes "
                                    "to '%s', which is not a stat",
                                    skill.id.c_str(), field.c_str());
                    continue;
                }
                mod.flat = float((*mt)["flat"].value_or(0.0));
                mod.percent = float((*mt)["percent"].value_or(0.0));
                skill.perLevel.push_back(mod);
            }
        }
        mSkills.push_back(std::move(skill));
    }

    std::sort(mSkills.begin(), mSkills.end(),
              [](const SkillDef& a, const SkillDef& b) {
                  return a.sortOrder != b.sortOrder ? a.sortOrder < b.sortOrder
                                                    : a.id < b.id;
              });
    eng::log::info("SkillTable: %d skills", int(mSkills.size()));
    return !mSkills.empty();
}

bool SkillTable::loadFromString(const char* tomlSrc)
{
    toml::parse_result parsed = toml::parse(tomlSrc);
    if (!parsed) {
        eng::log::error("SkillTable: %s",
                        std::string(parsed.error().description()).c_str());
        mSkills.clear();
        return false;
    }
    return parse(&parsed.table());
}

const SkillDef* SkillTable::find(const std::string& id) const
{
    const auto it = std::find_if(mSkills.begin(), mSkills.end(),
                                 [&](const SkillDef& s) { return s.id == id; });
    return it == mSkills.end() ? nullptr : &*it;
}

int SkillTable::indexOf(const std::string& id) const
{
    for (std::size_t i = 0; i < mSkills.size(); ++i)
        if (mSkills[i].id == id)
            return int(i);
    return -1;
}

// ---------------------------------------------------------------------------
// SkillSet
// ---------------------------------------------------------------------------

int64_t SkillSet::experience(const std::string& skill) const
{
    const auto it = mXp.find(skill);
    return it == mXp.end() ? 0 : it->second;
}

int SkillSet::level(const std::string& skill) const
{
    return xp::levelForXp(experience(skill));
}

int SkillSet::totalLevel() const
{
    if (!mTable)
        return 0;
    int total = 0;
    // Every defined skill counts, including the ones at level 1 with no
    // experience -- that is what makes "total level" a comparable number
    // between two characters rather than a count of what they have touched.
    for (const SkillDef& s : mTable->all())
        total += level(s.id);
    return total;
}

int64_t SkillSet::totalExperience() const
{
    int64_t total = 0;
    for (const auto& [id, value] : mXp)
        total += value;
    return total;
}

int SkillSet::award(const std::string& skill, int64_t amount)
{
    if (amount <= 0)
        return 0;
    if (mTable && !mTable->find(skill)) {
        eng::log::error("skills: '%s' is not a skill; %lld experience "
                        "discarded", skill.c_str(), (long long)amount);
        return 0;
    }
    int64_t& value = mXp[skill];
    const int before = xp::levelForXp(value);
    value = std::min(value + amount, xp::xpForLevel(xp::kMaxSkillLevel));
    const int after = xp::levelForXp(value);
    // Skill experience feeds the character level too: in Tarkov everything you
    // do moves the one number the world gates on.
    awardCharacter(amount);
    return after - before;
}

int SkillSet::awardCharacter(int64_t amount)
{
    if (amount <= 0)
        return 0;
    const int before = characterLevel();
    mCharacterXp = std::min(mCharacterXp + amount,
                            xp::xpForCharacterLevel(xp::kMaxCharacterLevel));
    return characterLevel() - before;
}

void SkillSet::setRaw(std::unordered_map<std::string, int64_t> xpById,
                      int64_t characterXp)
{
    mXp = std::move(xpById);
    mCharacterXp = std::max<int64_t>(0, characterXp);
}

void SkillSet::clear()
{
    mXp.clear();
    mCharacterXp = 0;
}

std::vector<StatModifier> SkillSet::modifiers() const
{
    std::vector<StatModifier> out;
    if (!mTable)
        return out;
    for (const SkillDef& skill : mTable->all()) {
        // Level 1 is the baseline everyone has, so a skill contributes for the
        // levels *above* it. Otherwise every character would start with the
        // full level-1 bonus of every skill in the game, and the numbers a
        // designer authored per level would be off by one everywhere.
        const int levels = level(skill.id) - 1;
        if (levels <= 0)
            continue;
        for (const StatModifier& m : skill.perLevel)
            out.push_back({m.field, m.flat * float(levels),
                           m.percent * float(levels)});
    }
    return out;
}

} // namespace game::rpg
