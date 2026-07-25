// Pure stamina gating + regen. No physics/renderer.
#include "../src/combat/StaminaSystem.h"
#include "../src/combat/FeelComponents.h"

#include <cmath>
#include <cstdio>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-2f; }

int main() {
    // spend succeeds when affordable and subtracts
    {
        Stamina s{}; s.current = 50.0f;
        check(feel::stamina::spend(s, 20.0f), "affordable spend ok");
        check(nearly(s.current, 30.0f), "spend subtracts");
        check(nearly(s.sinceSpend, 0.0f), "spend resets sinceSpend");
    }
    // spend fails when unaffordable and does not subtract
    {
        Stamina s{}; s.current = 10.0f;
        check(!feel::stamina::spend(s, 20.0f), "unaffordable spend rejected");
        check(nearly(s.current, 10.0f), "rejected spend leaves stamina");
    }
    // tick: no regen inside the delay window
    {
        entt::registry reg;
        auto e = reg.create();
        auto& s = reg.emplace<Stamina>(e); s.current = 40.0f; s.sinceSpend = 0.0f;
        feel::stamina::tick(reg, 0.2f); // < regenDelay 0.5
        check(nearly(reg.get<Stamina>(e).current, 40.0f), "no regen during delay");
    }
    // tick: regen after the delay, clamped to max
    {
        entt::registry reg;
        auto e = reg.create();
        auto& s = reg.emplace<Stamina>(e); s.current = 40.0f; s.sinceSpend = 1.0f;
        feel::stamina::tick(reg, 1.0f); // past delay: +35/s * 1s = +35
        check(nearly(reg.get<Stamina>(e).current, 75.0f), "regen after delay");
        feel::stamina::tick(reg, 10.0f);
        check(nearly(reg.get<Stamina>(e).current, 100.0f), "regen clamps at max");
    }

    if (failures == 0) std::printf("StaminaSystemTests OK\n");
    return failures ? 1 : 0;
}
