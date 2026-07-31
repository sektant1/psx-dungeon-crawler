#pragma once
#include "EnemyComponents.h"
#include "EnemyDef.h"

#include <glm/glm.hpp>

namespace game::enemyai {

// What the world told this enemy about itself and its target this tick. The
// brain never queries physics, the registry or the renderer: EnemySystem fills
// this in, calls think(), and applies the Intent. That is what makes the whole
// state machine testable with no engine at all -- and what stops "AI" from
// quietly becoming a second place that moves bodies around.
struct Senses {
    glm::vec3 selfPos{0.0f};   // feet
    float selfYaw = 0.0f;      // radians
    bool targetValid = false;
    glm::vec3 targetPos{0.0f}; // feet
    float targetDistance = 0.0f;
    bool lineOfSight = false;
    float healthPct = 1.0f;
    bool staggered = false;    // ActionState is Staggered
    bool dead = false;
    bool busy = false;         // ActionState is not Idle (mid-swing)
    // Crowd spacing, precomputed from neighbours. Added to the move vector so a
    // pack spreads out instead of stacking into one cylinder.
    glm::vec3 separation{0.0f};
};

// What the brain wants. EnemySystem turns this into velocity, a yaw slew and an
// actionstate::beginAttack call. Nothing here commits: an attack the stamina
// system refuses simply does not start, and the brain re-decides next tick.
struct Intent {
    glm::vec3 moveDirection{0.0f}; // world-space, normalized or zero
    float moveSpeed = 0.0f;        // m/s the enemy wants to travel at
    float desiredYaw = 0.0f;       // radians
    int attack = -1;               // index into def.attacks to begin this tick
    bool wantsProjectile = false;  // that attack is ranged
};

// Advance one enemy's brain by dt and say what it wants. Pure: reads only its
// arguments, writes only `brain`.
Intent think(const EnemyDef& def, EnemyBrain& brain, const Senses& senses,
             float dt);

// The attack the brain asked for actually started. Puts it on cooldown.
//
// This is separate from think() because an Intent is a request, not a fact:
// the feel layer can still refuse it for want of stamina. Starting the
// cooldown inside think() charged the enemy for swings it never made, so a
// stamina-starved enemy burnt its whole move list and then stood there with
// everything on cooldown.
void notifyAttackStarted(const EnemyDef& def, EnemyBrain& brain, int attack);

// The attack was refused. Drops the commitment and backs off to recover,
// leaving the move available for when there is stamina for it.
void notifyAttackRefused(EnemyBrain& brain);

// --- pieces, exposed because they are each worth testing on their own -------

// Can the enemy see the target right now? Range, facing cone, and line of
// sight, with a hearing radius that ignores facing.
bool canPerceive(const EnemyDef& def, const Senses& senses);

// Index of the move to commit to, or -1. Only moves that are off cooldown, in
// range and inside their aim cone are eligible; among those the pick is
// weighted. `roll` is a [0,1) value the caller supplies so this stays pure.
int selectAttack(const EnemyDef& def, const EnemyBrain& brain,
                 const Senses& senses, float roll);

// Yaw slew toward `desired`, capped at turnRateDeg per second. Handles wrap.
float turnToward(float current, float desired, float turnRateDeg, float dt);

// Yaw that faces `from` -> `to` in the controller's convention.
float yawToward(glm::vec3 from, glm::vec3 to);

} // namespace game::enemyai
