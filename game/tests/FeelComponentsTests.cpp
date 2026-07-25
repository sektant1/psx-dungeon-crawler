// Feel-layer component defaults + trivial invariants. Data only, no behavior.
#include "../src/combat/FeelComponents.h"

#include <cmath>
#include <cstdio>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main() {
    Stamina st{};
    check(nearly(st.current, 100.0f) && nearly(st.max, 100.0f), "stamina full by default");

    Poise po{};
    check(nearly(po.current, 100.0f), "poise full by default");
    check(nearly(po.staggerImmuneFor, 0.0f), "no stagger immunity by default");

    Mana mn{};
    check(nearly(mn.current, 100.0f), "mana full by default");

    ActionState as{};
    check(as.phase == ActionPhase::Idle, "starts idle");
    check(!as.activeFiredThisStep, "no active edge by default");

    AttackDef def{};
    check(def.windup > 0.0f && def.recovery > 0.0f, "attack has windup+recovery");

    check(feel::kStaggerDuration > 0.0f, "stagger duration positive");
    check(nearly(feel::kDeflectWindow, 0.15f), "deflect window is 150ms");

    if (failures == 0) std::printf("FeelComponentsTests OK\n");
    return failures ? 1 : 0;
}
