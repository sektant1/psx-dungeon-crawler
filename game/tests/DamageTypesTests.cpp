// Damage channels are content: this drives the shipped assets/magic.toml
// through CombatVocabulary, then checks that mitigation keys off the ids it
// hands out. A skeleton-style resist profile makes blunt the answer. Pure.
#include "../src/combat/CombatComponents.h"
#include "../src/combat/CombatVocabulary.h"
#include "../src/combat/DamageSystem.h"
#include "TestAssets.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-2f; }

int main() {
    game::test::mountGameAssets();
    CombatVocabulary vocab;
    check(vocab.load(game::test::asset("magic.toml")),
          "magic.toml failed to load");

    const DamageTypeId slash = vocab.damageType("slash");
    const DamageTypeId pierce = vocab.damageType("pierce");
    const DamageTypeId blunt = vocab.damageType("blunt");
    const DamageTypeId physical = vocab.damageType("physical");
    const DamageTypeId trueDmg = vocab.damageType("true");

    // the channels the combat design depends on are defined, and distinct
    check(slash != kInvalidDamageType, "slash channel is not defined");
    check(pierce != kInvalidDamageType, "pierce channel is not defined");
    check(blunt != kInvalidDamageType, "blunt channel is not defined");
    check(slash != pierce, "slash != pierce");
    check(blunt != physical, "blunt != generic physical");

    // an undefined channel is reported, not silently mapped onto channel 0
    check(vocab.damageType("nonexistent") == kInvalidDamageType,
          "an unknown channel resolved to something");

    // only the channel authored to bypass mitigation does
    check(vocab.bypassesMitigation(trueDmg), "true damage does not bypass");
    check(!vocab.bypassesMitigation(slash), "slash bypasses mitigation");

    // skeleton: resists slash+pierce (0.5), weak to blunt (-0.5)
    Resistances r{};
    r[slash] = 0.5f;
    r[pierce] = 0.5f;
    r[blunt] = -0.5f;
    check(nearly(damage::mitigate(100, slash, &r), 50), "skeleton halves slash");
    check(nearly(damage::mitigate(100, pierce, &r), 50), "skeleton halves pierce");
    check(nearly(damage::mitigate(100, blunt, &r), 150), "skeleton weak to blunt");
    check(nearly(damage::mitigate(100, trueDmg, &r,
                                  vocab.bypassesMitigation(trueDmg)),
                 100),
          "true still bypasses");

    // every school names a channel that exists, and there is a fallback school
    check(vocab.defaultSchool() != nullptr, "no schools of magic are defined");
    for (const char* id : {"fire", "frost", "poison", "arcane"}) {
        const MagicSchoolDef* school = vocab.school(id);
        check(school != nullptr, "a documented school is missing");
        if (school)
            check(vocab.damageTypeDef(school->damage) != nullptr,
                  "a school resolved to an undefined channel");
    }

    if (failures == 0) std::printf("DamageTypesTests OK\n");
    return failures ? 1 : 0;
}
