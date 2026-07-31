// Spawner pacing: arming, waves, gating, caps and respawn. Pure scheduling --
// no world, no enemies, just the decision.
#include "../src/enemy/EnemySpawner.h"

#include <cstdio>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}

int main()
{
    // Always-armed spawner populates immediately, once.
    {
        EnemySpawnPoint p;
        p.enemy = "hollow";
        p.count = 2;
        p.waves = 1;
        EnemySpawnState s;
        check(tickSpawner(p, s, 50.0f, 0, 0.1f).spawnCount == 2,
              "an always-armed spawner fires on the first tick");
        check(tickSpawner(p, s, 50.0f, 2, 0.1f).spawnCount == 0,
              "a one-wave spawner does not fire twice");
        check(s.exhausted, "spent after its last wave");
    }

    // Activation radius: nothing until the player is close.
    {
        EnemySpawnPoint p;
        p.enemy = "hollow";
        p.activationRadius = 10.0f;
        EnemySpawnState s;
        check(tickSpawner(p, s, 25.0f, 0, 0.1f).spawnCount == 0,
              "does not fire while the player is far");
        check(!s.armed, "stays disarmed while the player is far");
        const SpawnDecision d = tickSpawner(p, s, 5.0f, 0, 0.1f);
        check(d.armedNow, "arms when the player comes inside the radius");
        check(d.spawnCount == 1, "fires on the tick it arms (no arm delay)");
    }

    // Arm delay: the "they heard you" beat before the wave lands.
    {
        EnemySpawnPoint p;
        p.enemy = "hollow";
        p.activationRadius = 10.0f;
        p.armDelay = 0.5f;
        EnemySpawnState s;
        check(tickSpawner(p, s, 5.0f, 0, 0.1f).spawnCount == 0,
              "arm delay holds the first wave");
        int spawned = 0;
        for (int i = 0; i < 10; ++i)
            spawned += tickSpawner(p, s, 5.0f, 0, 0.1f).spawnCount;
        check(spawned == 1, "the wave lands once the delay expires");
    }

    // Deactivation: an armed spawner behind the player stops feeding.
    {
        EnemySpawnPoint p;
        p.enemy = "hollow";
        p.waves = 0; // endless
        p.activationRadius = 10.0f;
        p.deactivationRadius = 20.0f;
        EnemySpawnState s;
        tickSpawner(p, s, 5.0f, 0, 0.1f);
        const SpawnDecision d = tickSpawner(p, s, 30.0f, 0, 0.1f);
        check(d.disarmedNow, "disarms past the deactivation radius");
        check(!s.armed && d.spawnCount == 0, "and stops spawning");
    }

    // Wave gating: the next wave waits for the last one to be cleared.
    {
        EnemySpawnPoint p;
        p.enemy = "hollow";
        p.count = 3;
        p.waves = 3;
        p.clearBeforeNextWave = true;
        EnemySpawnState s;
        check(tickSpawner(p, s, 1.0f, 0, 0.1f).spawnCount == 3, "wave 1");
        check(tickSpawner(p, s, 1.0f, 3, 0.1f).spawnCount == 0,
              "wave 2 waits while wave 1 is alive");
        check(tickSpawner(p, s, 1.0f, 1, 0.1f).spawnCount == 0,
              "still waits with one left standing");
        check(tickSpawner(p, s, 1.0f, 0, 0.1f).spawnCount == 3,
              "wave 2 lands once the arena is clear");
    }

    // Pressure encounter: waves come on a timer whether or not you cleared.
    {
        EnemySpawnPoint p;
        p.enemy = "hollow";
        p.count = 1;
        p.waves = 0;
        p.waveDelay = 1.0f;
        p.clearBeforeNextWave = false;
        p.maxAlive = 10;
        EnemySpawnState s;
        check(tickSpawner(p, s, 1.0f, 0, 0.1f).spawnCount == 1, "first wave");
        int spawned = 0;
        for (int i = 0; i < 10; ++i)
            spawned += tickSpawner(p, s, 1.0f, 1, 0.1f).spawnCount;
        check(spawned == 1,
              "the next wave lands on its timer even with one still alive");
    }

    // maxAlive throttles rather than blocks: it tops up to the cap.
    {
        EnemySpawnPoint p;
        p.enemy = "hollow";
        p.count = 5;
        p.waves = 0;
        p.clearBeforeNextWave = false;
        p.maxAlive = 4;
        EnemySpawnState s;
        check(tickSpawner(p, s, 1.0f, 2, 0.1f).spawnCount == 2,
              "spawns only up to the cap");
        EnemySpawnState full;
        check(tickSpawner(p, full, 1.0f, 4, 0.1f).spawnCount == 0,
              "spawns nothing at the cap");
    }

    // Respawn: the whole spawner comes back, but only once it is empty.
    {
        EnemySpawnPoint p;
        p.enemy = "hollow";
        p.waves = 1;
        p.respawnDelay = 1.0f;
        EnemySpawnState s;
        tickSpawner(p, s, 1.0f, 0, 0.1f); // wave 1
        check(s.exhausted, "spent");
        tickSpawner(p, s, 1.0f, 1, 0.1f);
        check(s.exhausted, "does not start respawning while one is alive");
        tickSpawner(p, s, 1.0f, 0, 0.1f); // empty: start the clock
        check(!s.exhausted, "respawn clock started");
        int spawned = 0;
        for (int i = 0; i < 20; ++i)
            spawned += tickSpawner(p, s, 1.0f, 0, 0.1f).spawnCount;
        check(spawned == 1, "respawns one wave after the delay");
    }

    // No respawn delay means gone for good.
    {
        EnemySpawnPoint p;
        p.enemy = "hollow";
        p.waves = 1;
        EnemySpawnState s;
        tickSpawner(p, s, 1.0f, 0, 0.1f);
        int spawned = 0;
        for (int i = 0; i < 50; ++i)
            spawned += tickSpawner(p, s, 1.0f, 0, 0.1f).spawnCount;
        check(spawned == 0, "a spent spawner with no respawn stays spent");
    }

    // Markers: "enemy.<id>" and "enemy.<id>.<preset>".
    {
        EnemySpawner spawner;
        EnemySpawnPoint ambush;
        ambush.count = 4;
        ambush.activationRadius = 12.0f;
        spawner.addPreset("ambush", ambush);

        std::vector<EnemySpawner::Marker> markers;
        markers.push_back({"enemy.hollow", glm::vec3(1.0f, 0.0f, 2.0f), 0.0f});
        markers.push_back({"enemy.hound.ambush", glm::vec3(0.0f), 0.0f});
        markers.push_back({"physics.barrel", glm::vec3(0.0f), 0.0f});
        check(spawner.addFromMarkers(markers) == 2,
              "only enemy.* markers become spawn points");
        check(spawner.point(0).enemy == "hollow", "plain marker names the enemy");
        check(spawner.point(0).position.x == 1.0f, "marker position is kept");
        check(spawner.point(1).enemy == "hound", "preset marker names the enemy");
        check(spawner.point(1).count == 4, "preset supplies the pacing");
        check(spawner.point(1).activationRadius == 12.0f, "and the arming");
    }

    if (failures == 0) std::printf("EnemySpawnerTests OK\n");
    return failures ? 1 : 0;
}
