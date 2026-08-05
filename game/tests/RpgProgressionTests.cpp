// The two progression tables, and how a skill level reaches a combat number.
//
// The OSRS table is asserted against its published values rather than against
// this implementation's own output. Reimplementing that formula wrongly
// produces a table that looks right at level 20 and is tens of thousands of
// points out at level 80, and only the known landmarks catch that.
#include "../src/rpg/Skills.h"
#include "../src/rpg/Stats.h"

#include <cmath>
#include <cstdio>

using namespace game::rpg;

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) < eps;
}

int main()
{
    // --- the OSRS table, at its published landmarks ------------------------
    check(xp::xpForLevel(1) == 0, "level 1 is free");
    check(xp::xpForLevel(2) == 83, "level 2 costs 83");
    check(xp::xpForLevel(3) == 174, "level 3 costs 174");
    check(xp::xpForLevel(10) == 1154, "level 10 costs 1,154");
    check(xp::xpForLevel(50) == 101333, "level 50 costs 101,333");
    check(xp::xpForLevel(60) == 273742, "level 60 costs 273,742");
    check(xp::xpForLevel(70) == 737627, "level 70 costs 737,627");
    check(xp::xpForLevel(92) == 6517253, "level 92 costs 6,517,253");
    check(xp::xpForLevel(99) == 13034431, "level 99 costs 13,034,431");

    // The inverse agrees at every boundary, and one point short of each.
    for (int level = 1; level <= xp::kMaxSkillLevel; ++level) {
        const int64_t need = xp::xpForLevel(level);
        check(xp::levelForXp(need) == level, "levelForXp lands on the boundary");
        if (level > 1)
            check(xp::levelForXp(need - 1) == level - 1,
                  "one xp short is the level below");
    }
    check(xp::levelForXp(0) == 1, "no experience is level 1");
    check(xp::levelForXp(999999999) == 99, "the table caps at 99");

    // Level 92 being half of 99 is the property the curve is known for.
    check(std::llabs(xp::xpForLevel(92) * 2 - xp::xpForLevel(99)) < 100,
          "92 is within a rounding error of half of 99");

    check(xp::xpToNextLevel(0) == 83, "83 to the first level");
    check(xp::xpToNextLevel(xp::xpForLevel(99)) == 0, "nothing left at the cap");
    check(nearly(xp::levelProgress(0), 0.0f), "no progress at zero");
    check(nearly(xp::levelProgress(xp::xpForLevel(50)), 0.0f),
          "a fresh level starts at zero progress");

    // --- the character table ------------------------------------------------
    check(xp::characterLevelForXp(0) == 1, "character starts at 1");
    check(xp::xpForCharacterLevel(1) == 0, "character level 1 is free");
    check(xp::xpForCharacterLevel(2) == 1000, "character level 2 costs 1,000");
    for (int level = 2; level <= xp::kMaxCharacterLevel; ++level)
        check(xp::xpForCharacterLevel(level) >
                  xp::xpForCharacterLevel(level - 1),
              "the character table is strictly increasing");
    // The two tables are deliberately different shapes: the character cap has
    // to be reachable in a campaign, a skill cap in a career.
    check(xp::xpForCharacterLevel(xp::kMaxCharacterLevel) <
              xp::xpForLevel(xp::kMaxSkillLevel),
          "the character cap is cheaper than a maxed skill");

    // --- a table, and awarding against it -----------------------------------
    SkillTable table;
    check(table.loadFromString(R"(
[[skill]]
id = "blades"
name = "Blades"
order = 0
[[skill.per_level]]
stat = "might"
flat = 0.5

[[skill]]
id = "lockpicking"
name = "Lockpicking"
order = 1
)"),
          "the skill table loads");
    check(table.size() == 2, "two skills");
    check(table.find("blades") != nullptr, "blades is defined");
    check(table.find("nonesuch") == nullptr, "an undefined skill is not found");

    SkillSet skills;
    skills.setTable(&table);
    check(skills.level("blades") == 1, "an untouched skill is level 1");
    // Every defined skill counts toward the total, including the untouched
    // ones -- that is what makes total level comparable between characters.
    check(skills.totalLevel() == 2, "total level counts every defined skill");

    check(skills.award("blades", 83) == 1, "83 xp is exactly one level");
    check(skills.level("blades") == 2, "and the level reads 2");
    check(skills.award("blades", 1) == 0, "one more xp is no level");
    check(skills.totalLevel() == 3, "the total moved with it");

    // A single award that pays for several levels grants all of them.
    SkillSet leap;
    leap.setTable(&table);
    check(leap.award("blades", xp::xpForLevel(10)) == 9,
          "one award can buy nine levels");

    // Skill experience feeds the character level too: in Tarkov everything you
    // do moves the one number the world gates on.
    check(leap.characterExperience() == xp::xpForLevel(10),
          "skill xp also accrues to the character");
    check(leap.characterLevel() > 1, "and it moved the character level");

    // An unknown skill is refused rather than silently creating a track, so a
    // typo in a weapon's `trains` field is visible.
    SkillSet strict;
    strict.setTable(&table);
    check(strict.award("nonesuch", 10000) == 0, "an unknown skill is refused");
    check(strict.experience("nonesuch") == 0, "and stores nothing");

    // Experience cannot exceed the cap, so a huge award cannot overflow the
    // level lookup.
    SkillSet capped;
    capped.setTable(&table);
    capped.award("blades", 999999999999LL);
    check(capped.level("blades") == 99, "the cap holds");

    // --- skills reach the derived block through the modifier layer ----------
    ProgressionCurve curve;
    CharacterSheet sheet;
    sheet.setCurve(curve);

    SkillSet trained;
    trained.setTable(&table);
    trained.award("blades", xp::xpForLevel(11)); // level 11 = ten levels above 1

    const float baseMight = sheet.derived().attributes[StatField::Might];
    sheet.setModifiers(stats::kSkillModifierSource, trained.modifiers());
    const float trainedMight = sheet.derived().attributes[StatField::Might];
    // 0.5 per level, for the ten levels above 1.
    check(nearly(trainedMight - baseMight, 5.0f),
          "ten levels of a 0.5/level skill is +5 might");

    // Re-pushing the same source is idempotent: this is what makes "just
    // re-apply everything" the correct response to any change.
    sheet.setModifiers(stats::kSkillModifierSource, trained.modifiers());
    check(nearly(sheet.derived().attributes[StatField::Might], trainedMight),
          "re-applying the skill group does not double-count");

    // A skill with no per_level contributes nothing -- a number that only opens
    // doors is a legitimate skill.
    SkillSet picker;
    picker.setTable(&table);
    picker.award("lockpicking", xp::xpForLevel(40));
    check(picker.modifiers().empty(),
          "a gate-only skill contributes no modifiers");
    check(picker.level("lockpicking") == 40, "but it still levels");

    if (failures == 0)
        std::printf("RpgProgressionTests OK\n");
    return failures == 0 ? 0 : 1;
}
