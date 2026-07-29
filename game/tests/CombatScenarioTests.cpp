// Balance targets from the combat-redesign spec §8, asserted by composing the
// feel systems with the existing damage resolver. Pure/deterministic.
#include "../src/combat/DamageSystem.h"
#include "../src/combat/PoiseSystem.h"
#include "../src/combat/DefenseSystem.h"
#include "../src/combat/ActionStateSystem.h"
#include "../src/combat/CombatComponents.h"
#include "../src/combat/FeelComponents.h"

#include <cstdio>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}

// A trash goblin: 30 HP, 20 poise.
static entt::entity goblin(entt::registry& reg) {
    auto e = reg.create();
    reg.emplace<Health>(e, Health{30.0f, 30.0f, 0.0f});
    reg.emplace<FactionTag>(e, FactionTag{Faction::Enemy});
    auto& p = reg.emplace<Poise>(e); p.current = 20.0f; p.max = 20.0f;
    reg.emplace<ActionState>(e);
    return e;
}

int main() {
    // TARGET: goblin dies in <=2 clean 22-dmg sword hits.
    {
        entt::registry reg;
        auto g = goblin(reg);
        DamagePacket p; p.amount = 22.0f; p.type = 0;
        auto r1 = damage::apply(reg, g, p);
        check(!r1.killed, "one hit does not kill goblin");
        auto r2 = damage::apply(reg, g, p);
        check(r2.killed, "two clean hits kill a goblin");
    }

    // TARGET: one heavy (poise 45) staggers a goblin (poise 20).
    {
        entt::registry reg;
        auto g = goblin(reg);
        bool broke = feel::poise::apply(reg, g, 45.0f);
        check(broke, "heavy staggers goblin in one blow");
        check(reg.get<ActionState>(g).phase == ActionPhase::Staggered, "goblin staggered");
    }

    // TARGET: player (100 HP) dies in <=3 unblocked ~40-dmg goblin hits.
    {
        entt::registry reg;
        auto pl = reg.create();
        reg.emplace<Health>(pl, Health{100.0f, 100.0f, 0.0f});
        reg.emplace<FactionTag>(pl, FactionTag{Faction::Player});
        DamagePacket hit; hit.amount = 40.0f; hit.type = 0;
        damage::apply(reg, pl, hit);
        damage::apply(reg, pl, hit);
        check(!reg.get<Health>(pl).dead(), "player survives two goblin hits");
        auto r3 = damage::apply(reg, pl, hit);
        check(r3.killed, "third unblocked hit kills the player");
    }

    // TARGET: deflect loop — player deflects a goblin's blow -> goblin staggers
    // -> punish crit kills. Composes defense + poise + damage.
    {
        entt::registry reg;
        auto g = goblin(reg); // 20 poise; deflect punish is 45 -> breaks
        auto pl = reg.create();
        auto& pas = reg.emplace<ActionState>(pl);
        reg.emplace<FactionTag>(pl, FactionTag{Faction::Player});

        feel::defense::beginDeflect(pas);
        bool clean = feel::defense::resolveIncoming(reg, pl, g, 20.0f);
        check(clean, "in-window deflect negates the goblin blow");
        check(reg.get<ActionState>(g).phase == ActionPhase::Staggered,
              "deflect staggers the goblin");

        // punish: crit hit while staggered (goblin at 30 HP, 22 * 2.0 crit = 44)
        DamagePacket punish; punish.amount = 44.0f; punish.crit = true;
        punish.source = pl;
        auto r = damage::apply(reg, g, punish);
        check(r.killed, "staggered goblin dies to the punish");
    }

    if (failures == 0) std::printf("CombatScenarioTests OK\n");
    return failures ? 1 : 0;
}
