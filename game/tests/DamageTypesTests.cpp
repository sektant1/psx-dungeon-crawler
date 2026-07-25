// Slash/Pierce/Blunt physical channels: enum present, mitigation keys them, and
// a skeleton-style resist profile makes blunt the answer. Pure.
#include "../src/combat/DamageSystem.h"
#include "../src/combat/CombatComponents.h"
#include "../src/combat/DamageTypes.h"

#include <cmath>
#include <cstdio>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-2f; }

int main() {
    // the three channels exist and are distinct
    check(DamageType::Slash != DamageType::Pierce, "slash != pierce");
    check(DamageType::Blunt != DamageType::Physical, "blunt != generic physical");

    // skeleton: resists slash+pierce (0.5), weak to blunt (-0.5)
    Resistances r{};
    r[DamageType::Slash] = 0.5f;
    r[DamageType::Pierce] = 0.5f;
    r[DamageType::Blunt] = -0.5f;
    check(nearly(damage::mitigate(100, DamageType::Slash, &r), 50), "skeleton halves slash");
    check(nearly(damage::mitigate(100, DamageType::Pierce, &r), 50), "skeleton halves pierce");
    check(nearly(damage::mitigate(100, DamageType::Blunt, &r), 150), "skeleton weak to blunt");
    check(nearly(damage::mitigate(100, DamageType::True, &r), 100), "true still bypasses");

    if (failures == 0) std::printf("DamageTypesTests OK\n");
    return failures ? 1 : 0;
}
