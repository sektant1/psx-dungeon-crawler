// WeaponLibrary parses the new timing/draw fields from TOML.
#include "../src/combat/WeaponLibrary.h"
#include "../src/combat/WeaponDef.h"

#include <cmath>
#include <cstdio>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main() {
    const char* toml =
        "[weapon.sword]\n"
        "base_damage = 22.0\n"
        "windup = 0.25\n"
        "active = 0.06\n"
        "recovery = 0.35\n"
        "stamina_cost = 15.0\n"
        "poise_damage = 20.0\n"
        "is_sweep = false\n"
        "[weapon.greatsword]\n"
        "base_damage = 40.0\n"
        "windup = 0.5\n"
        "poise_damage = 45.0\n"
        "is_sweep = true\n"
        "arc = 1.05\n"
        "[weapon.bow]\n"
        "base_damage = 18.0\n"
        "draw_time = 0.8\n"
        "full_draw_mult = 2.5\n";

    WeaponLibrary lib;
    lib.loadFromString(toml);

    const WeaponDef& sword = lib.get("sword");
    check(nearly(sword.timing.windup, 0.25f), "sword windup parsed");
    check(nearly(sword.timing.recovery, 0.35f), "sword recovery parsed");
    check(nearly(sword.timing.staminaCost, 15.0f), "sword stamina parsed");
    check(nearly(sword.timing.poiseDamage, 20.0f), "sword poise parsed");
    check(!sword.timing.isSweep, "sword not a sweep");

    const WeaponDef& gs = lib.get("greatsword");
    check(gs.timing.isSweep, "greatsword is sweep");
    check(nearly(gs.timing.arc, 1.05f), "greatsword arc parsed");

    const WeaponDef& bow = lib.get("bow");
    check(nearly(bow.drawTime, 0.8f), "bow draw time parsed");
    check(nearly(bow.fullDrawMult, 2.5f), "bow full-draw mult parsed");

    if (failures == 0) std::printf("WeaponTimingTests OK\n");
    return failures ? 1 : 0;
}
