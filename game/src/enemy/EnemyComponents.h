#pragma once
#include "EnemyDef.h"

#include <eng/Handles.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace game {

// Enemy state lives on the *combat* registry, next to Health/Poise/ActionState.
// One registry means an enemy is a combatant that happens to have a brain, not a
// parallel object with its own health that has to be kept in sync with the
// combat model's.

// The behaviour states. Attack does not have its own timer: the swing runs on
// ActionState (windup/active/recovery), the same machine the player's swings
// run on, so an enemy attack can be deflected, punished and interrupted by the
// rules that already exist.
enum class EnemyState {
    Dormant,     // asleep; wakes on sight (ambush)
    Idle,        // no target
    Alert,       // just noticed something; the readable reaction beat
    Chase,       // closing to preferred range
    Circle,      // strafing at range, waiting out a cooldown
    Attack,      // committed; ActionState owns the timing
    Reposition,  // deliberate backstep/spacing beat after a trade
    Search,      // lost line of sight; walking to the last known position
    Flee,        // broken; running away
    Stagger,     // poise broken; ActionState owns the timing
    Dead,
};

const char* enemyStateName(EnemyState);

// Which def this enemy came from, plus the id for logs and serialisation.
//
// The definition is shared, not borrowed: reloading enemies.toml destroys the
// table's definitions, and this outlives a frame. Holding a share means a live
// enemy keeps the row it was spawned with even if that row is edited out of
// the file mid-session, which is the only behaviour that cannot crash.
struct EnemyTag {
    std::shared_ptr<const EnemyDef> def;
    std::string defId;
};

// Everything the brain remembers between ticks. Deterministic: `rng` is a
// per-enemy xorshift seeded from the spawn index, so two runs of the same fight
// play out identically (the physics world already guarantees this).
struct EnemyBrain {
    EnemyState state = EnemyState::Idle;
    float stateTime = 0.0f;   // seconds in the current state
    float thinkTimer = 0.0f;  // counts down to the next full re-decision
    float lostSightFor = 0.0f;
    glm::vec3 home{0.0f};          // spawn position, for leashing
    glm::vec3 lastKnownTarget{0.0f};
    bool hasLastKnown = false;
    float strafeSign = 1.0f;       // which way it circles this beat
    int pendingAttack = -1;        // index into def.attacks, -1 = none
    float cooldown[8] = {0.0f};    // per-attack cooldown, indexed like attacks
    uint32_t rng = 0x9E3779B9u;
    float aggroGrace = 0.0f;       // brief "stay aggressive" window after a hit
};

// Physical state the AI drives and EnemySystem integrates. Split from the brain
// because the brain is pure and this is what touches the world.
struct EnemyMotion {
    glm::vec3 velocity{0.0f}; // horizontal, m/s
    float yaw = 0.0f;         // radians, +Z forward like the player controller
    bool grounded = true;
    // Feet position, refreshed from physics at the top of every step. Cached so
    // crowd separation is an O(n^2) pass over a small vector instead of n^2
    // physics transform reads.
    glm::vec3 feet{0.0f};
};

// Render-side handles and the presentation timers the AI never reads.
struct EnemyRender {
    eng::NodeHandle node;
    eng::MeshHandle mesh;
    float hitFlash = 0.0f;    // counts down; drives the squash pulse
    float corpseTimer = 0.0f; // counts down once dead; 0 = permanent corpse
    bool dying = false;
};

// Which spawner produced this enemy, so a spawner knows when its wave is clear
// without scanning the whole registry.
struct EnemyOrigin {
    int spawnerIndex = -1;
};

// Transient execution state for a committed ranged sequence. Boss phase logic
// and ordinary enemy AI can both start the same authored pattern API.
struct EnemyPatternExecution {
    int attackIndex = -1;
    BulletPatternState pattern;
};

// An enemy projectile in flight. It is an entity on the same registry, not a
// special case inside the enemy loop: a bolt outlives its shooter, and making
// it an entity is what lets it.
//
// Enemy projectiles do not use Jolt bodies. A bolt is a point moving in a
// straight line for under a second; a swept ray against the world and a
// segment-distance test against the player answer both collision questions
// exactly, with no body to create, layer to add or contact callback to route.
// The player is a character controller, so it has no body to hit anyway --
// which is the actual reason a physics projectile would not have worked.
struct EnemyProjectile {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float radius = 0.18f;
    float ttl = 4.0f;
    std::string weapon;             // row in weapons.toml
    entt::entity owner = entt::null;
    eng::NodeHandle node;
};

// Deterministic xorshift32; the AI's only randomness source.
inline uint32_t nextRandom(uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
inline float randomUnit(uint32_t& s)
{
    return float(nextRandom(s) & 0xFFFFFFu) / float(0x1000000u);
}

} // namespace game
