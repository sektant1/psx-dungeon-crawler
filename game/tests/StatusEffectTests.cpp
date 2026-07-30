// StatusEffectSystem tests: Burn DoT (Fire-resisted), expiry, movement/action
// gating from Stun/Root/Slow/Silence. No renderer/physics.
#include "../src/combat/StatusEffectSystem.h"
#include "../src/combat/CombatComponents.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-2f; }

// The burn channel is content; the system takes whichever id it is handed.
constexpr DamageTypeId kBurnChannel = 1;

int main() {
    // --- Burn deals damage over time, mitigated by Fire resist ---
    {
        entt::registry reg;
        auto e = reg.create();
        reg.emplace<Health>(e, Health{100, 100, 0});
        Resistances r{}; r[kBurnChannel] = 0.5f; // half burn
        reg.emplace<Resistances>(e, r);
        auto& fx = reg.emplace<StatusEffects>(e);
        fx.active.push_back({CrowdControl::Burn, 10.0f, 2.0f, 0.0f, entt::null}); // 10 dps, 2s

        std::vector<entt::entity> killed;
        // Advance 2s in one step: 10 dps * 2s = 20 raw, halved by resist = 10.
        status::tick(reg, 2.0f, killed, {kBurnChannel});
        check(nearly(reg.get<Health>(e).current, 90.0f), "burn dot resisted");
        check(reg.get<StatusEffects>(e).active.empty(), "burn expired after duration");
    }

    // --- Burn can kill and reports the entity ---
    {
        entt::registry reg;
        auto e = reg.create();
        reg.emplace<Health>(e, Health{5, 100, 0});
        auto& fx = reg.emplace<StatusEffects>(e);
        fx.active.push_back({CrowdControl::Burn, 20.0f, 3.0f, 0.0f, entt::null});
        std::vector<entt::entity> killed;
        status::tick(reg, 1.0f, killed);
        check(!killed.empty() && killed[0] == e, "burn kill reported");
        check(reg.get<Health>(e).current <= 0.0f, "burn dropped hp to zero");
    }

    // --- movement multiplier: slow stacks multiplicatively, stun/root zero it ---
    {
        entt::registry reg;
        auto e = reg.create();
        auto& fx = reg.emplace<StatusEffects>(e);
        fx.active.push_back({CrowdControl::Slow, 0.5f, 5.0f, 0.0f, entt::null});
        fx.active.push_back({CrowdControl::Chill, 0.5f, 5.0f, 0.0f, entt::null});
        check(nearly(status::movementMultiplier(reg, e), 0.25f), "slow*chill = 0.25");

        fx.active.push_back({CrowdControl::Root, 0.0f, 5.0f, 0.0f, entt::null});
        check(nearly(status::movementMultiplier(reg, e), 0.0f), "root zeroes movement");
    }

    // --- action/cast gates ---
    {
        entt::registry reg;
        auto e = reg.create();
        auto& fx = reg.emplace<StatusEffects>(e);
        fx.active.push_back({CrowdControl::Silence, 0.0f, 5.0f, 0.0f, entt::null});
        check(status::canAct(reg, e), "silence still allows attacks");
        check(!status::canCast(reg, e), "silence blocks casting");

        fx.active.push_back({CrowdControl::Stun, 0.0f, 5.0f, 0.0f, entt::null});
        check(!status::canAct(reg, e), "stun blocks actions");
        check(!status::canCast(reg, e), "stun blocks casting");
    }

    // --- no StatusEffects component = unaffected defaults ---
    {
        entt::registry reg;
        auto e = reg.create();
        check(nearly(status::movementMultiplier(reg, e), 1.0f), "no effects = full speed");
        check(status::canAct(reg, e), "no effects = can act");
        check(status::canCast(reg, e), "no effects = can cast");
    }

    if (failures == 0) std::printf("StatusEffectTests OK\n");
    return failures ? 1 : 0;
}
