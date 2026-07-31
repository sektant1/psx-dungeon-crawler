#pragma once
#include "EnemyComponents.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace game {

struct GameContext;
class EnemySystem;
class EnemySpawner;

// Persistence for the enemy layer.
//
// The shape here is deliberate: a plain snapshot struct, a *pure* codec over
// it, and world-touching capture/restore built on top. The codec is what gets
// versioned and tested; it compiles and round-trips with no renderer, no
// physics world and no registry, so the format can be exercised exhaustively
// and cheaply. Only `capture` and `restore` need a live game.
//
// WHAT IS SAVED
//   - which definition each enemy came from, by id
//   - where it is, which way it faces, how fast it is going
//   - health, poise, stamina and their timers
//   - the whole brain: state, timers, last-known target, per-move cooldowns,
//     and the RNG stream (so a reloaded fight is not a different fight)
//   - active status effects (burns, slows) with their remaining durations
//   - which spawner produced it, and every spawner's wave/arming state
//
// WHAT IS DELIBERATELY NOT SAVED, and why
//   - Corpses. They are decoration on a despawn timer; restoring one means
//     restoring a dynamic body's full transform and velocity to reproduce a
//     ragdoll mid-tumble, which nobody will notice and which cannot be made
//     to look identical anyway.
//   - Projectiles in flight. Sub-second lifetime; saving mid-flight bolts buys
//     nothing and makes the format carry entity references it would then have
//     to remap.
//   - The in-progress swing (ActionState). A save landing between an enemy's
//     windup and its active frame would restore a half-committed attack whose
//     telegraph the player never saw. Enemies reload Idle -- readable, and it
//     never resurrects a hit the player had no chance to react to.
//   - `ActiveEffect::source`. Entity ids are not stable across a save, and the
//     status tick does not read source (only kind/magnitude/remaining), so it
//     is dropped rather than remapped. Attribution of a kill to whoever lit
//     the fire is lost across a save; nothing else is.
//
// CONTENT DRIFT
// A save references definitions and spawners by id, never by index, because a
// save outlives the file it was made against. An enemy whose definition has
// since been deleted or renamed is dropped with a log rather than guessed at;
// a spawner whose id is gone keeps its authored default state.

// One enemy, as bytes-in-waiting. Plain data: no handles, no pointers, no
// entity ids.
struct EnemySnapshot {
    std::string defId;
    glm::vec3 feet{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    bool grounded = true;

    float health = 0.0f;
    float healthMax = 0.0f;
    float invulnTimer = 0.0f;
    float poise = 0.0f;
    float poiseMax = 0.0f;
    float poiseSinceHit = 0.0f;
    float staggerImmuneFor = 0.0f;
    float stamina = 0.0f;
    float staminaMax = 0.0f;
    float staminaSinceSpend = 0.0f;

    // Brain. Stored flat rather than as an EnemyBrain so the on-disk layout is
    // not hostage to a component's field order.
    uint8_t state = 0; // EnemyState
    float stateTime = 0.0f;
    float thinkTimer = 0.0f;
    float lostSightFor = 0.0f;
    glm::vec3 home{0.0f};
    glm::vec3 lastKnownTarget{0.0f};
    bool hasLastKnown = false;
    float strafeSign = 1.0f;
    float cooldown[8] = {0.0f};
    uint32_t rng = 1;
    float aggroGrace = 0.0f;

    int32_t spawnerIndex = -1;
    std::string spawnerId; // authoritative; index is a hint that can shift

    struct Effect {
        uint8_t kind = 0; // CrowdControl
        float magnitude = 0.0f;
        float remaining = 0.0f;
        float tickAccum = 0.0f;
    };
    std::vector<Effect> effects;
};

// One spawner's pacing state, keyed by its authored id.
struct SpawnerSnapshot {
    std::string id;
    bool armed = false;
    bool exhausted = false;
    int32_t wavesSpawned = 0;
    float timer = 0.0f;
    int32_t spawnedTotal = 0;
};

struct EnemySaveData {
    std::vector<EnemySnapshot> enemies;
    std::vector<SpawnerSnapshot> spawners;
};

namespace enemysave {

// Bump when the record layout changes. `decode` refuses anything it does not
// recognise rather than reading a field that has moved -- a save that loads
// wrong is worse than one that refuses to load.
inline constexpr uint16_t kVersion = 1;

// --- the pure half ---------------------------------------------------------

// Serialise. Self-contained: magic, version, string pool, then the records.
std::vector<uint8_t> encode(const EnemySaveData&);

// Deserialise. Returns nothing for a blob that is truncated, has the wrong
// magic, or was written by a different version -- never a partly-filled
// result, so a caller cannot half-restore a fight.
std::optional<EnemySaveData> decode(const uint8_t* data, std::size_t size);
inline std::optional<EnemySaveData> decode(const std::vector<uint8_t>& bytes)
{
    return decode(bytes.data(), bytes.size());
}

// Convenience file wrappers over the codec. Both log and return false/nothing
// on failure rather than throwing; a save that cannot be written must not take
// the game down with it.
bool writeFile(const std::string& path, const EnemySaveData&);
std::optional<EnemySaveData> readFile(const std::string& path);

// --- the half that needs a world -------------------------------------------

// Read the live enemy archetype and spawner states into a snapshot. Skips
// corpses.
EnemySaveData capture(const EnemySystem&, const EnemySpawner&);

// Wipe whatever is live and rebuild it from `data`. Enemies come back through
// EnemySystem::spawn -- the same path a fresh spawn takes, so a restored enemy
// cannot end up with a body or node built differently from a spawned one --
// and their saved state is then written over the defaults.
//
// Returns the number of enemies restored; ones whose definition no longer
// exists are skipped and logged.
int restore(GameContext&, EnemySystem&, EnemySpawner&, const EnemySaveData&);

} // namespace enemysave

} // namespace game
