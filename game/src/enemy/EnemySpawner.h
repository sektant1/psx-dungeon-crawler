#pragma once
#include "audio/ActorSounds.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace game {

struct GameContext;
class EnemySystem;

// A place enemies come from, as data.
//
// One struct covers the shapes a crawler actually needs: a single sleeping
// enemy in a room, an encounter that arms when you walk in, an arena that
// releases waves, and a corridor that keeps trickling. They are the same
// mechanism with different numbers -- an ambush is a wave spawner with one wave,
// a patrol is a wave spawner with respawn.
struct EnemySpawnPoint {
    std::string id;
    std::string enemy;          // definition id in EnemyLibrary
    glm::vec3 position{0.0f};   // feet, world space
    float yaw = 0.0f;           // radians the spawned enemies face

    int count = 1;              // enemies per wave
    float scatter = 1.2f;       // metres they are spread over around `position`
    int waves = 1;              // 0 = endless
    float waveDelay = 0.0f;     // seconds between waves once the last is cleared
    // With clearBeforeNextWave off, waves come on a timer whether or not the
    // previous one is dead -- which is how a pressure encounter differs from a
    // wave-clear arena.
    bool clearBeforeNextWave = true;
    // Cap on this spawner's live enemies; also throttles endless spawners.
    int maxAlive = 8;

    // Distance from the player at which the spawner arms. 0 = always armed
    // (it populates the level at load).
    float activationRadius = 0.0f;
    // Distance past which an armed spawner disarms again, so a spawner behind
    // the player stops feeding the fight. 0 = never disarms.
    float deactivationRadius = 0.0f;
    // Seconds after the last enemy of the last wave dies before the whole
    // spawner resets. 0 = never respawns.
    float respawnDelay = 0.0f;
    // Delay after arming before the first wave lands (the "they heard you" beat).
    float armDelay = 0.0f;

    // Cue overrides stamped on every enemy this point produces, over whatever
    // the enemy type authors. Set from an authored placement's sound table;
    // empty for spawners defined in the spawner TOML, which get the type's.
    ActorSoundSet sounds;
};

// Mutable per-spawner state. Split from the definition so the definition stays
// reloadable and this stays serialisable for a future save system.
struct EnemySpawnState {
    bool armed = false;
    bool exhausted = false;  // all waves spent; only respawnDelay can revive it
    int wavesSpawned = 0;
    float timer = 0.0f;      // counts down to the next wave / the respawn
    int aliveFromHere = 0;   // refreshed each tick from the live enemy list
    int spawnedTotal = 0;
};

// What one spawner wants to do this tick. Pure decision, separated from the
// spawning so the pacing rules are testable without a physics world.
struct SpawnDecision {
    int spawnCount = 0;
    bool armedNow = false;
    bool disarmedNow = false;
};

// Advance one spawner's schedule. `distance` is to the player; `aliveFromHere`
// is how many of its enemies are still standing. Pure: reads its arguments,
// writes only `state`.
SpawnDecision tickSpawner(const EnemySpawnPoint& point, EnemySpawnState& state,
                          float distance, int aliveFromHere, float dt);

// The live set of spawners in the current level. It owns pacing and nothing
// else: creating an enemy is EnemySystem's job, and the two meet at one call.
class EnemySpawner {
public:
    void clear();
    // Register a spawn point. Returns its index, which is what an enemy carries
    // in EnemyOrigin so the spawner can count its own dead.
    int add(const EnemySpawnPoint& point);

    // Load `[[spawn]]` entries from a TOML document. Returns how many were
    // added. Missing/malformed files add nothing and are logged.
    int loadFromToml(const std::string& tomlPath);

    // Turn level markers into spawn points. A marker named "enemy.hollow" spawns
    // one "hollow" where the marker is; "enemy.hollow.pack" additionally looks
    // up a `[preset.pack]` in the spawner file, so level authoring stays in the
    // level and pacing stays in the data.
    struct Marker {
        std::string type;     // full marker name, e.g. "enemy.hollow"
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        ActorSoundSet sounds; // authored per-placement cue overrides
    };
    int addFromMarkers(const std::vector<Marker>& markers);

    // Advance every spawner and spawn what they ask for.
    void update(GameContext& ctx, EnemySystem& enemies, glm::vec3 playerFeet,
                float dt);

    int size() const { return int(mPoints.size()); }
    const EnemySpawnPoint& point(int i) const { return mPoints[size_t(i)]; }
    const EnemySpawnState& state(int i) const { return mStates[size_t(i)]; }
    EnemySpawnPoint& mutablePoint(int i) { return mPoints[size_t(i)]; }
    // Write access for save/load. Restoring pacing is the whole reason a
    // reloaded save does not re-trigger every ambush you already cleared.
    EnemySpawnState& mutableState(int i) { return mStates[size_t(i)]; }
    // Index of the point with this authored id, or -1. Saves key by id
    // because an index shifts the moment spawners.toml gains an entry.
    int indexOf(const std::string& id) const;
    // Force a wave now, ignoring arming and pacing. Debug panel / scripting.
    void trigger(GameContext& ctx, EnemySystem& enemies, int index);

    // Named presets that markers can refer to, so "an ambush" is authored once.
    void addPreset(const std::string& name, const EnemySpawnPoint& tmpl);

private:
    void emit(GameContext& ctx, EnemySystem& enemies, int index, int count);

    std::vector<EnemySpawnPoint> mPoints;
    std::vector<EnemySpawnState> mStates;
    std::vector<std::pair<std::string, EnemySpawnPoint>> mPresets;
    std::uint32_t mRng = 0x1234567u;
};

} // namespace game
