// Pure poise chip -> stagger, regen, and post-stagger immunity.
#include "../src/combat/PoiseSystem.h"
#include "../src/combat/FeelComponents.h"

#include <cmath>
#include <cstdio>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-2f; }

static entt::entity actor(entt::registry& reg, float poise) {
    auto e = reg.create();
    auto& p = reg.emplace<Poise>(e); p.current = poise; p.max = poise;
    reg.emplace<ActionState>(e);
    return e;
}

int main() {
    // chip below break threshold: no stagger
    {
        entt::registry reg;
        auto e = actor(reg, 20.0f);
        bool broke = feel::poise::apply(reg, e, 5.0f);
        check(!broke, "small chip does not break");
        check(nearly(reg.get<Poise>(e).current, 15.0f), "poise chipped");
        check(reg.get<ActionState>(e).phase == ActionPhase::Idle, "still idle");
        check(nearly(reg.get<Poise>(e).sinceHit, 0.0f), "hit resets sinceHit");
    }
    // chip past zero: stagger, poise resets to max, timer set
    {
        entt::registry reg;
        auto e = actor(reg, 20.0f);
        bool broke = feel::poise::apply(reg, e, 25.0f);
        check(broke, "overkill chip breaks poise");
        auto& as = reg.get<ActionState>(e);
        check(as.phase == ActionPhase::Staggered, "staggered on break");
        check(nearly(as.timer, feel::kStaggerDuration), "stagger timer set");
        check(nearly(reg.get<Poise>(e).current, 20.0f), "poise refilled on break");
    }
    // immunity blocks further chip
    {
        entt::registry reg;
        auto e = actor(reg, 20.0f);
        reg.get<Poise>(e).staggerImmuneFor = 0.5f;
        bool broke = feel::poise::apply(reg, e, 100.0f);
        check(!broke, "no break while immune");
        check(nearly(reg.get<Poise>(e).current, 20.0f), "poise untouched while immune");
    }
    // tick: regen after delay, immunity counts down
    {
        entt::registry reg;
        auto e = actor(reg, 20.0f);
        auto& p = reg.get<Poise>(e);
        p.current = 5.0f; p.sinceHit = 2.0f; p.staggerImmuneFor = 0.3f;
        feel::poise::tick(reg, 0.5f); // past delay 1.0? sinceHit already 2.0 -> regen
        check(reg.get<Poise>(e).current > 5.0f, "poise regens after delay");
        check(nearly(reg.get<Poise>(e).staggerImmuneFor, 0.0f), "immunity counts down to 0");
    }

    if (failures == 0) std::printf("PoiseSystemTests OK\n");
    return failures ? 1 : 0;
}
