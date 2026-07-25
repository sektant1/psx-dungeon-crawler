// Phase machine: begin (pays stamina) -> Windup -> Active (single edge) ->
// Recovery -> Idle; stagger times out to Idle and grants poise immunity.
#include "../src/combat/ActionStateSystem.h"
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
    // beginAttack pays stamina and enters Windup with the def
    {
        entt::registry reg;
        auto e = reg.create();
        auto& as = reg.emplace<ActionState>(e);
        auto& st = reg.emplace<Stamina>(e); st.current = 100.0f;
        AttackDef def; def.windup = 0.2f; def.active = 0.06f; def.recovery = 0.3f;
        def.staminaCost = 15.0f;
        check(feel::actionstate::beginAttack(as, st, def), "begin ok when idle+stamina");
        check(as.phase == ActionPhase::Windup, "enters windup");
        check(nearly(as.timer, 0.2f), "windup timer set");
        check(nearly(st.current, 85.0f), "stamina paid");
    }
    // beginAttack rejected mid-action or when broke
    {
        entt::registry reg;
        auto e = reg.create();
        auto& as = reg.emplace<ActionState>(e); as.phase = ActionPhase::Windup;
        auto& st = reg.emplace<Stamina>(e); st.current = 100.0f;
        check(!feel::actionstate::beginAttack(as, st, AttackDef{}), "no begin mid-action");
        as.phase = ActionPhase::Idle; st.current = 5.0f;
        AttackDef def; def.staminaCost = 15.0f;
        check(!feel::actionstate::beginAttack(as, st, def), "no begin when broke");
    }
    // advance: Windup -> Active fires the edge exactly once, then Recovery, Idle
    {
        entt::registry reg;
        auto e = reg.create();
        auto& as = reg.emplace<ActionState>(e);
        as.phase = ActionPhase::Windup; as.timer = 0.1f;
        as.attack.active = 0.05f; as.attack.recovery = 0.1f;
        feel::actionstate::advance(reg, 0.1f); // windup expires -> Active this step
        check(as.phase == ActionPhase::Active, "windup -> active");
        check(as.activeFiredThisStep, "active edge fired");
        feel::actionstate::advance(reg, 0.001f); // still active, edge cleared
        check(!as.activeFiredThisStep, "edge is one-shot");
        feel::actionstate::advance(reg, 0.05f); // active expires -> Recovery
        check(as.phase == ActionPhase::Recovery, "active -> recovery");
        feel::actionstate::advance(reg, 0.1f); // recovery expires -> Idle
        check(as.phase == ActionPhase::Idle, "recovery -> idle");
    }
    // advance: stagger times out to Idle and grants poise immunity
    {
        entt::registry reg;
        auto e = reg.create();
        auto& as = reg.emplace<ActionState>(e);
        as.phase = ActionPhase::Staggered; as.timer = 0.1f;
        auto& po = reg.emplace<Poise>(e);
        feel::actionstate::advance(reg, 0.1f);
        check(as.phase == ActionPhase::Idle, "stagger -> idle");
        check(nearly(po.staggerImmuneFor, feel::kPostStaggerImmunity), "immunity granted");
    }

    if (failures == 0) std::printf("ActionStateSystemTests OK\n");
    return failures ? 1 : 0;
}
