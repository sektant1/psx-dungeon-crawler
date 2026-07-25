#pragma once
#include <entt/entt.hpp>

namespace game {

// The one action state a combatant is in. Windup is the readable tell; Active is
// the (brief) live-hitbox frame; Recovery is the punish window; Staggered is the
// locked, crit-vulnerable opening. Deflecting/Dodging are timed defensive states.
enum class ActionPhase {
    Idle,
    Windup,
    Active,
    Recovery,
    Deflecting,
    Dodging,
    Staggered,
};

namespace feel {
// Balance constants (spec 2026-07-24 combat-redesign, section 8). Tunable.
inline constexpr float kStaggerDuration    = 1.0f;  // seconds locked when poise breaks
inline constexpr float kPostStaggerImmunity = 0.5f; // poise-immunity after recovering
inline constexpr float kDeflectWindow      = 0.15f; // 150ms clean-deflect window
} // namespace feel

// Gates actions (melee swing, bow draw, dodge, kick). Regens after regenDelay
// seconds of no spend. `sinceSpend` counts up each tick.
struct Stamina {
    float current = 100.0f;
    float max = 100.0f;
    float regenRate = 35.0f;   // per second
    float regenDelay = 0.5f;   // seconds after a spend before regen resumes
    float sinceSpend = 999.0f; // time since last spend (starts "rested")
};

// Gates being staggered. Regens after regenDelay seconds of no hit. While
// staggerImmuneFor > 0, poise cannot be chipped (post-stagger grace).
struct Poise {
    float current = 100.0f;
    float max = 100.0f;
    float regenRate = 30.0f;    // per second
    float regenDelay = 1.0f;    // seconds after a hit before regen resumes
    float sinceHit = 999.0f;    // time since last poise damage
    float staggerImmuneFor = 0.0f; // remaining immunity window
};

// Cast-only resource. Slow passive regen.
struct Mana {
    float current = 100.0f;
    float max = 100.0f;
    float regenRate = 10.0f; // per second
};

// Timing + cost payload of one attack (a weapon's light/heavy, an enemy swing).
// isSweep = arc hit (multi-target melee); arc = half-angle radians of the sweep.
struct AttackDef {
    float windup = 0.2f;
    float active = 0.06f;
    float recovery = 0.3f;
    float staminaCost = 15.0f;
    float poiseDamage = 20.0f;
    bool isSweep = false;
    float arc = 0.0f; // radians (half-angle); meaningful when isSweep
};

// The per-entity state machine. `timer` counts down the current phase. `attack`
// is the in-flight attack (valid during Windup/Active/Recovery). activeFired-
// ThisStep is set true by actionstate::advance on the single Windup->Active edge
// so the delivery layer (CombatSystem) knows exactly when to resolve the hit.
struct ActionState {
    ActionPhase phase = ActionPhase::Idle;
    float timer = 0.0f;
    AttackDef attack{};
    bool activeFiredThisStep = false;
};

} // namespace game
