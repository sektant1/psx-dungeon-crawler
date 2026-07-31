#pragma once
#include "combat/CombatVocabulary.h"
#include "combat/BulletPattern.h"
#include "combat/DamageTypes.h"
#include "combat/FeelComponents.h"

#include <glm/glm.hpp>

#include <string>
#include <optional>
#include <vector>

namespace game {

// What an enemy *is*, as data. Nothing here knows about Ogre, Jolt or EnTT: an
// EnemyDef is the authored row that EnemyLibrary parses out of enemies.toml and
// that EnemySystem turns into a body, a node and a set of components.
//
// The split into small structs is deliberate. A "type of enemy" is a
// combination of a body, a brain and a move list, not a C++ subclass, so a new
// enemy is a new TOML table -- and an enemy that only differs in one dimension
// (a faster grunt, a tankier archer) inherits an archetype and overrides that
// one field. There is no enemy-specific code anywhere in the game.

// Collision/hit volume. Enemies are upright capsules, the same shape and the
// same convention as the player's character controller (and as Unity's capsule
// collider): `height` is the TOTAL height including both hemispherical caps,
// and the straight section is whatever is left over. A capsule rather than a
// cylinder because a cylinder's hard bottom rim catches on every step edge and
// doorway lip, and because the player is a capsule -- two different shapes
// means the player and an enemy of identical stated size do not fit through
// the same gap.
//
// The silhouette is the hitbox: the drawn mesh is built from these exact
// numbers, so what you can see is what you can hit.
struct EnemyBody {
    float radius = 0.35f;
    float height = 1.8f; // total, caps included
    float mass = 70.0f;
    // How far above the feet the AI's "eyes" sit, for line-of-sight rays.
    float eyeHeight = 1.5f;
    // How much it fights being pushed into by other enemies (crowd spacing).
    float separationRadius = 0.9f;

    // The straight (cylindrical) section, which is what both Jolt's
    // CapsuleShape and the primitive mesh generator actually take. Derived in
    // one place so the body and the mesh cannot disagree about what `height`
    // meant -- the failure that produces a hitbox taller than the model.
    //
    // Always strictly positive. A radius of half the height or more would be a
    // sphere, and a sphere is a reasonable enemy shape -- but the engine's
    // validPrimitiveMeshDesc rejects a zero height outright, so such a mesh
    // would silently fall back to the prototype box and the drawn shape would
    // stop matching the collider. Leaving a sliver of straight section keeps
    // the two in agreement and looks like the sphere it was asked for.
    float straightHeight() const
    {
        const float straight = height - 2.0f * cappedRadius();
        return straight > kMinStraight ? straight : kMinStraight;
    }
    // Radius clamped so the capsule can never exceed `height`.
    float cappedRadius() const
    {
        const float maxRadius = (height - kMinStraight) * 0.5f;
        return radius < maxRadius ? radius : (maxRadius > 0.0f ? maxRadius
                                                               : height * 0.5f);
    }

private:
    static constexpr float kMinStraight = 0.01f;
};

// Presentation. Placeholder art is a tinted primitive; every field here is a
// render-side concern the AI never reads, so swapping to authored meshes later
// touches EnemySystem's spawn path and nothing else.
struct EnemyVisual {
    std::string material = "Game/Enemy/Red";
    // Optional mesh (OBJ) instead of the primitive cylinder. Empty = cylinder.
    std::string mesh;
    glm::vec3 scale{1.0f};
    // Squash/stretch amplitude applied for `hitFlashTime` seconds when hit.
    float hitFlash = 0.16f;
    float hitFlashTime = 0.12f;
    bool castShadows = true;
    // Which profile in blood.toml this creature bleeds. A name, not a colour:
    // the profile decides spray, gibs, decal and pool together, so an undead
    // that sheds ichor is a one-word change here rather than four effects.
    // Empty means it does not bleed at all (constructs, ghosts).
    std::string blood = "human";
};

// Everything that decides how long it survives and how hard it is to interrupt.
// `resistances` is authored by damage-channel *name*; EnemyLibrary::resolve
// turns those into the ids that Resistances stores.
struct EnemyResistance {
    std::string channel;
    float value = 0.0f; // -1..0.9; positive resists, negative is a weakness
};

struct EnemyStats {
    float health = 60.0f;
    float poise = 40.0f;
    float poiseRegen = 30.0f;
    float stamina = 100.0f;
    float staminaRegen = 25.0f;
    std::vector<EnemyResistance> resistances;
    // Impulse the corpse gets on death, along the killing blow's direction,
    // plus a fixed upward bias so it tumbles rather than sliding.
    float deathImpulse = 6.0f;
    // Seconds the corpse stays before it is despawned. 0 = never (permanent).
    float corpseTime = 12.0f;
};

// How it moves. Ground speeds are m/s; acceleration is m/s^2 toward the AI's
// desired velocity, which is what stops a charge from turning on a dime.
struct EnemyLocomotion {
    float walkSpeed = 1.6f;   // patrol / search
    float chaseSpeed = 3.4f;  // closing on the target
    float strafeSpeed = 2.2f; // circling at preferred range
    float acceleration = 14.0f;
    float turnRateDeg = 360.0f; // yaw slew, deg/s
    // Extra speed multiplier applied for the duration of an attack's windup,
    // i.e. the lunge. 1 = no lunge.
    float lungeSpeed = 0.0f;
    // How far it can step up onto ledges/stairs while walking.
    float stepHeight = 0.4f;
};

// What it can notice, and for how long it stays interested. This is the whole
// "aggro" model: there is no separate aggro table.
struct EnemyPerception {
    float sightRange = 18.0f;
    float sightFovDeg = 140.0f;
    // A target closer than this is noticed regardless of facing (footsteps).
    float hearingRange = 5.0f;
    // Seconds without line of sight before it gives up the chase and searches
    // the last known position.
    float loseSightTime = 4.0f;
    // Seconds spent reacting (the readable "it saw me" beat) before it commits.
    float alertTime = 0.45f;
    // Distance from its spawn point past which it disengages and returns. 0 =
    // unleashed (it follows you across the level).
    float leashRange = 0.0f;
};

// The dials that make two enemies with identical stats fight differently. This
// is where a "type" actually lives: a Souls-like brute is aggression 1.0 with
// no circling, a skirmisher is aggression 0.4 with a wide circle band.
struct EnemyBehaviour {
    // 0..1. Scales the pause between attacks: 1 attacks the instant an attack
    // is off cooldown, 0 waits a full extra cycle.
    float aggression = 0.6f;
    // Distance it prefers to hold when not committing to an attack. 0 means
    // "derive it from the longest attack range", which is what most melee
    // enemies want.
    float preferredRange = 0.0f;
    // Inside this it backs off instead of crowding (kiting for ranged types).
    float backoffRange = 0.0f;
    // 0..1 chance that, after an attack, it circles instead of re-closing.
    float circleChance = 0.35f;
    // Seconds a circle/reposition beat lasts before it re-decides.
    float repositionTime = 1.1f;
    // Health fraction below which it flees. 0 = fights to the death.
    float fleeHealthPct = 0.0f;
    // 0..1 chance to sidestep when the target attacks from close range.
    float dodgeChance = 0.0f;
    // Seconds between full re-decisions. Bigger = more committed, more readable.
    float thinkInterval = 0.2f;
    // Starts asleep and only wakes when the player is within sightRange and in
    // line of sight -- the ambush/statue enemy.
    bool startsDormant = false;
    // Never moves (turret/trap). Still turns, telegraphs and attacks.
    bool stationary = false;
};

// One move in the enemy's move list. `timing` is the same AttackDef the player
// uses, so an enemy swing runs through the identical windup/active/recovery
// state machine and can be deflected, punished and staggered by the same rules.
struct EnemyAttack {
    std::string id = "swing";
    // Damage payload, by weapon id in weapons.toml. Enemies and the player draw
    // from one weapon table: an enemy "weapon" is just a row in it.
    std::string weapon = "sword";
    std::string telegraphEffect = "enemy_attack_telegraph";
    float minRange = 0.0f; // below this the move is not selectable
    float maxRange = 2.2f;
    // Half-angle (degrees) the target must be within for the move to be chosen.
    float aimConeDeg = 45.0f;
    float cooldown = 1.6f;
    // Relative selection weight among all currently valid moves.
    float weight = 1.0f;
    // Ranged moves spawn a projectile at the active frame instead of testing a
    // melee arc. The projectile itself is a CombatConfig-driven entity.
    bool ranged = false;
    float projectileSpeed = 18.0f;
    std::optional<BulletPatternDef> pattern;
    AttackDef timing{};
};

// The full authored row.
struct EnemyDef {
    std::string id;              // table key, e.g. "hollow_soldier"
    std::string name = "Enemy";  // shown by the look tooltip
    std::string category;        // flavour line ("Undead - shielded")
    std::string inherits;        // archetype id this row starts from
    // Ranking, purely for presentation/pacing (health bars, music, spawn cost).
    int tier = 1;
    bool boss = false;

    EnemyBody body;
    EnemyVisual visual;
    EnemyStats stats;
    EnemyLocomotion locomotion;
    EnemyPerception perception;
    EnemyBehaviour behaviour;
    std::vector<EnemyAttack> attacks;

    // Resolved once by EnemyLibrary::resolve against the vocabulary. Kept
    // alongside the authored names so a reload can re-resolve without
    // reparsing.
    float resistanceById[kMaxDamageTypes] = {0.0f};

    // Preferred standoff distance, resolving the "0 = derive it" rule.
    float preferredRange() const
    {
        if (behaviour.preferredRange > 0.0f)
            return behaviour.preferredRange;
        float longest = 0.0f;
        for (const EnemyAttack& a : attacks)
            longest = a.maxRange > longest ? a.maxRange : longest;
        return longest > 0.0f ? longest * 0.85f : 2.0f;
    }
};

} // namespace game
