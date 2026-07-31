#include "EnemySpawner.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>

namespace game {

namespace {

float num(const toml::table& t, const char* key, float fb)
{
    return float(t[key].value_or(double(fb)));
}

void readPoint(const toml::table& t, EnemySpawnPoint& p)
{
    p.id = t["id"].value_or(p.id);
    p.enemy = t["enemy"].value_or(p.enemy);
    if (const toml::array* a = t["position"].as_array(); a && a->size() == 3)
        for (size_t i = 0; i < 3; ++i)
            p.position[int(i)] = float((*a)[i].value_or(0.0));
    p.yaw = glm::radians(num(t, "yaw_degrees", glm::degrees(p.yaw)));
    p.count = t["count"].value_or(p.count);
    p.scatter = num(t, "scatter", p.scatter);
    p.waves = t["waves"].value_or(p.waves);
    p.waveDelay = num(t, "wave_delay", p.waveDelay);
    p.clearBeforeNextWave =
        t["clear_before_next_wave"].value_or(p.clearBeforeNextWave);
    p.maxAlive = t["max_alive"].value_or(p.maxAlive);
    p.activationRadius = num(t, "activation_radius", p.activationRadius);
    p.deactivationRadius = num(t, "deactivation_radius", p.deactivationRadius);
    p.respawnDelay = num(t, "respawn_delay", p.respawnDelay);
    p.armDelay = num(t, "arm_delay", p.armDelay);
}

} // namespace

SpawnDecision tickSpawner(const EnemySpawnPoint& p, EnemySpawnState& s,
                          float distance, int aliveFromHere, float dt)
{
    SpawnDecision out;
    s.aliveFromHere = aliveFromHere;

    // Arming. A spawner with no activation radius is armed from the first tick,
    // which is what "the level is already populated" means.
    if (!s.armed) {
        if (p.activationRadius <= 0.0f || distance <= p.activationRadius) {
            s.armed = true;
            s.timer = p.armDelay;
            out.armedNow = true;
        } else {
            return out;
        }
    } else if (p.deactivationRadius > 0.0f && distance > p.deactivationRadius) {
        s.armed = false;
        out.disarmedNow = true;
        return out;
    }

    if (s.timer > 0.0f) {
        s.timer -= dt;
        if (s.timer > 0.0f)
            return out;
        s.timer = 0.0f;
    }

    if (s.exhausted) {
        // Spent. Only a respawn delay can bring it back, and only once the last
        // of its enemies is gone -- otherwise a slow fight would be reinforced
        // by its own spawner.
        if (p.respawnDelay <= 0.0f || aliveFromHere > 0)
            return out;
        s.timer = p.respawnDelay;
        s.exhausted = false;
        s.wavesSpawned = 0;
        return out;
    }

    if (p.waves > 0 && s.wavesSpawned >= p.waves) {
        s.exhausted = true;
        return out;
    }
    if (s.wavesSpawned > 0 && p.clearBeforeNextWave && aliveFromHere > 0)
        return out;

    const int room = p.maxAlive > 0 ? p.maxAlive - aliveFromHere : p.count;
    const int n = std::min(p.count, room);
    if (n <= 0)
        return out;

    out.spawnCount = n;
    ++s.wavesSpawned;
    s.spawnedTotal += n;
    s.timer = p.waveDelay;
    if (p.waves > 0 && s.wavesSpawned >= p.waves)
        s.exhausted = true;
    return out;
}

void EnemySpawner::clear()
{
    mPoints.clear();
    mStates.clear();
}

int EnemySpawner::add(const EnemySpawnPoint& p)
{
    mPoints.push_back(p);
    mStates.emplace_back();
    return int(mPoints.size()) - 1;
}

int EnemySpawner::indexOf(const std::string& id) const
{
    if (id.empty())
        return -1;
    for (size_t i = 0; i < mPoints.size(); ++i)
        if (mPoints[i].id == id)
            return int(i);
    return -1;
}

void EnemySpawner::addPreset(const std::string& name,
                             const EnemySpawnPoint& tmpl)
{
    mPresets.emplace_back(name, tmpl);
}

int EnemySpawner::loadFromToml(const std::string& tomlPath)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("EnemySpawner: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        return 0;
    }
    const toml::table& root = parsed.table();

    // Presets first: a spawn entry may name one.
    if (const toml::table* presets = root["preset"].as_table()) {
        for (auto&& [key, node] : *presets) {
            const toml::table* t = node.as_table();
            if (!t)
                continue;
            EnemySpawnPoint tmpl;
            readPoint(*t, tmpl);
            addPreset(std::string(key.str()), tmpl);
        }
    }

    int added = 0;
    const toml::array* spawns = root["spawn"].as_array();
    if (!spawns)
        return 0;
    for (const toml::node& node : *spawns) {
        const toml::table* t = node.as_table();
        if (!t)
            continue;
        EnemySpawnPoint p;
        // `preset = "ambush"` seeds every field before the entry's own overrides.
        if (auto preset = (*t)["preset"].value<std::string>()) {
            bool found = false;
            for (const auto& [name, tmpl] : mPresets)
                if (name == *preset) {
                    p = tmpl;
                    found = true;
                    break;
                }
            if (!found)
                eng::log::error("EnemySpawner: unknown preset '%s'",
                                preset->c_str());
        }
        readPoint(*t, p);
        if (p.enemy.empty()) {
            eng::log::error("EnemySpawner: spawn entry with no enemy id");
            continue;
        }
        add(p);
        ++added;
    }
    eng::log::info("EnemySpawner: %d spawn points from %s", added,
                   tomlPath.c_str());
    return added;
}

int EnemySpawner::addFromMarkers(const std::vector<Marker>& markers)
{
    int added = 0;
    for (const Marker& m : markers) {
        // "enemy.<id>" or "enemy.<id>.<preset>".
        if (m.type.rfind("enemy.", 0) != 0)
            continue;
        const std::string rest = m.type.substr(6);
        const size_t dot = rest.find('.');
        const std::string enemyId = dot == std::string::npos
                                        ? rest
                                        : rest.substr(0, dot);
        const std::string presetName =
            dot == std::string::npos ? std::string() : rest.substr(dot + 1);
        if (enemyId.empty())
            continue;

        EnemySpawnPoint p;
        if (!presetName.empty()) {
            bool found = false;
            for (const auto& [name, tmpl] : mPresets)
                if (name == presetName) {
                    p = tmpl;
                    found = true;
                    break;
                }
            if (!found)
                eng::log::error(
                    "EnemySpawner: marker '%s' names preset '%s', which the "
                    "spawner file does not define; using defaults",
                    m.type.c_str(), presetName.c_str());
        }
        p.id = m.type;
        p.enemy = enemyId;
        p.position = m.position;
        p.yaw = m.yaw;
        add(p);
        ++added;
    }
    return added;
}

} // namespace game
