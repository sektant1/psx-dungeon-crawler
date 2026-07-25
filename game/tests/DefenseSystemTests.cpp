// Deflect (negate + poise-punish), dodge (i-frames via Health.invulnTimer),
// kick (knockback impulse + poise chip). Pure; no physics/renderer.
#include "../src/combat/DefenseSystem.h"
#include "../src/combat/PoiseSystem.h"
#include "../src/combat/FeelComponents.h"
#include "../src/combat/CombatComponents.h"

#include <cmath>
#include <cstdio>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-2f; }

int main() {
    // beginDeflect enters the Deflecting state when idle
    {
        entt::registry reg;
        auto e = reg.create();
        auto& as = reg.emplace<ActionState>(e);
        check(feel::defense::beginDeflect(as), "deflect starts from idle");
        check(as.phase == ActionPhase::Deflecting, "in deflecting state");
        check(nearly(as.timer, feel::kDeflectWindow), "deflect timer = window");
    }
    // clean deflect: defender deflecting -> negate + chip attacker poise a lot
    {
        entt::registry reg;
        auto def = reg.create();
        auto& das = reg.emplace<ActionState>(def); das.phase = ActionPhase::Deflecting;
        das.timer = 0.1f;
        auto atk = reg.create();
        reg.emplace<ActionState>(atk);
        auto& ap = reg.emplace<Poise>(atk); ap.current = 100.0f; ap.max = 100.0f;
        bool clean = feel::defense::resolveIncoming(reg, def, atk, 60.0f);
        check(clean, "in-window hit is deflected");
        check(reg.get<Poise>(atk).current < 100.0f, "attacker poise punished");
    }
    // not deflecting: incoming is not negated
    {
        entt::registry reg;
        auto def = reg.create();
        reg.emplace<ActionState>(def); // Idle
        auto atk = reg.create();
        check(!feel::defense::resolveIncoming(reg, def, atk, 60.0f), "idle defender eats hit");
    }
    // dodge: pays stamina, enters Dodging, sets i-frames on Health
    {
        entt::registry reg;
        auto e = reg.create();
        auto& as = reg.emplace<ActionState>(e);
        auto& st = reg.emplace<Stamina>(e); st.current = 100.0f;
        auto& h = reg.emplace<Health>(e);
        check(feel::defense::beginDodge(reg, e, 0.4f, 0.2f), "dodge starts");
        check(as.phase == ActionPhase::Dodging, "in dodging state");
        check(nearly(st.current, 75.0f), "dodge costs 25 stamina");
        check(h.invulnTimer > 0.0f, "i-frames granted");
    }
    // kick: returns a knockback impulse along dir and chips target poise
    {
        entt::registry reg;
        auto target = reg.create();
        reg.emplace<ActionState>(target);
        auto& tp = reg.emplace<Poise>(target); tp.current = 100.0f; tp.max = 100.0f;
        glm::vec3 imp = feel::defense::kick(reg, target, glm::vec3(1, 0, 0), 8.0f, 15.0f);
        check(nearly(imp.x, 8.0f), "kick impulse along dir");
        check(reg.get<Poise>(target).current < 100.0f, "kick chips poise");
    }

    if (failures == 0) std::printf("DefenseSystemTests OK\n");
    return failures ? 1 : 0;
}
