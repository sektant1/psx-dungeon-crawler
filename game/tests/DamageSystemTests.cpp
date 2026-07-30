// Pure combat-resolver tests: resistance math, crit, friendly-fire, invuln,
// crowd-control application, death. No renderer/physics.
#include "../src/combat/DamageSystem.h"
#include "../src/combat/CombatComponents.h"

#include <cmath>
#include <cstdio>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-3f; }

static entt::entity makeTarget(entt::registry& reg, float hp, Faction f) {
    auto e = reg.create();
    reg.emplace<Health>(e, Health{hp, hp, 0.0f});
    reg.emplace<FactionTag>(e, FactionTag{f});
    return e;
}

// Channel ids for the mechanism under test. mitigate() cares that ids index a
// resistance row, not what any of them mean, so these are deliberately local
// rather than read out of assets/magic.toml.
constexpr DamageTypeId kNeutral = 0;
constexpr DamageTypeId kFire = 1;
constexpr DamageTypeId kFrost = 2;

int main() {
    // --- mitigate() math ---
    Resistances r{};
    r[kFire] = 0.5f;    // half fire
    r[kFrost] = -0.5f;  // +50% frost
    check(nearly(damage::mitigate(100, kNeutral, &r), 100), "neutral passes full");
    check(nearly(damage::mitigate(100, kFire, &r), 50), "50% resist halves");
    check(nearly(damage::mitigate(100, kFrost, &r), 150), "negative resist amplifies");
    check(nearly(damage::mitigate(100, kFire, &r, /*ignoresResistances=*/true), 100),
          "a bypassing channel ignores resist");
    r[kFire] = 5.0f; // clamps to 0.9
    check(nearly(damage::mitigate(100, kFire, &r), 10), "resist clamps at 0.9");
    check(nearly(damage::mitigate(100, kNeutral, nullptr), 100), "null resist = full");

    // --- apply(): basic damage + resist ---
    {
        entt::registry reg;
        auto t = makeTarget(reg, 100, Faction::Enemy);
        reg.emplace<Resistances>(t, Resistances{}); // neutral
        DamagePacket p; p.amount = 30; p.type = kNeutral;
        auto res = damage::apply(reg, t, p);
        check(res.hitLanded, "hit lands");
        check(nearly(res.dealt, 30), "full damage dealt");
        check(!res.killed, "not killed");
        check(nearly(reg.get<Health>(t).current, 70), "hp reduced");
    }

    // --- crit flag passes through, kill detection ---
    {
        entt::registry reg;
        auto t = makeTarget(reg, 25, Faction::Enemy);
        DamagePacket p; p.amount = 30; p.type = kNeutral; p.crit = true;
        auto res = damage::apply(reg, t, p);
        check(res.crit, "crit reported");
        check(res.killed, "lethal hit kills");
        check(reg.get<Health>(t).current <= 0.0f, "hp at/below zero");
    }

    // --- invulnerability blocks ---
    {
        entt::registry reg;
        auto t = makeTarget(reg, 50, Faction::Enemy);
        reg.get<Health>(t).invulnTimer = 1.0f;
        DamagePacket p; p.amount = 30;
        auto res = damage::apply(reg, t, p);
        check(!res.hitLanded, "invuln blocks");
        check(nearly(reg.get<Health>(t).current, 50), "hp unchanged under invuln");
    }

    // --- friendly fire gate ---
    {
        entt::registry reg;
        auto src = reg.create(); reg.emplace<FactionTag>(src, FactionTag{Faction::Player});
        auto ally = makeTarget(reg, 50, Faction::Player);
        DamagePacket p; p.amount = 30; p.source = src;
        auto res = damage::apply(reg, ally, p);
        check(!res.hitLanded, "same faction blocked");
        // enemy of the same source still takes damage
        auto foe = makeTarget(reg, 50, Faction::Enemy);
        auto res2 = damage::apply(reg, foe, p);
        check(res2.hitLanded, "cross-faction lands");
    }

    // --- CC application ---
    {
        entt::registry reg;
        auto t = makeTarget(reg, 50, Faction::Enemy);
        DamagePacket p; p.amount = 5;
        p.applies.push_back({CrowdControl::Stun, 0.0f, 1.5f});
        p.applies.push_back({CrowdControl::Burn, 4.0f, 3.0f});
        damage::apply(reg, t, p);
        auto* fx = reg.try_get<StatusEffects>(t);
        check(fx && fx->active.size() == 2, "two effects applied");
        check(fx->active[0].kind == CrowdControl::Stun, "stun stored");
        check(nearly(fx->active[1].remaining, 3.0f), "burn duration stored");
    }

    // --- dead target rejects further hits ---
    {
        entt::registry reg;
        auto t = makeTarget(reg, 10, Faction::Enemy);
        DamagePacket p; p.amount = 30;
        damage::apply(reg, t, p); // kills
        auto res = damage::apply(reg, t, p); // already dead
        check(!res.hitLanded, "dead target rejects");
    }

    if (failures == 0) std::printf("DamageSystemTests OK\n");
    return failures ? 1 : 0;
}
