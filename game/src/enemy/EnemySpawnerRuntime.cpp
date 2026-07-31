// The half of EnemySpawner that needs a world: creating enemies.
//
// Split from EnemySpawner.cpp on purpose. The pacing rules (tickSpawner, wave
// gating, presets, marker parsing) depend on nothing but their arguments, so
// they compile and test without a renderer, a physics world or an EnemySystem.
// Only these three functions actually spawn anything.
#include "EnemySpawner.h"

#include "EnemyComponents.h"
#include "EnemySystem.h"
#include "GameContext.h"

#include <eng/Log.h>

#include <cmath>

namespace game {

void EnemySpawner::emit(GameContext& ctx, EnemySystem& enemies, int index,
                        int count)
{
    const EnemySpawnPoint& p = mPoints[size_t(index)];
    for (int i = 0; i < count; ++i) {
        glm::vec3 at = p.position;
        if (p.scatter > 0.0f && count > 1) {
            // Ring, not a disc: a pack that lands on top of itself has to spend
            // its first second pushing apart instead of fighting.
            const float angle = 6.2831853f *
                                (float(i) / float(count) +
                                 randomUnit(mRng) * 0.15f);
            const float radius = p.scatter * (0.5f + 0.5f * randomUnit(mRng));
            at += glm::vec3(std::cos(angle) * radius, 0.0f,
                            std::sin(angle) * radius);
        }
        enemies.spawn(ctx, p.enemy, at, p.yaw, index);
    }
    eng::log::info("EnemySpawner: '%s' released %d x %s (%d alive)",
                   p.id.empty() ? p.enemy.c_str() : p.id.c_str(), count,
                   p.enemy.c_str(), enemies.aliveCount());
}

void EnemySpawner::trigger(GameContext& ctx, EnemySystem& enemies, int index)
{
    if (index < 0 || index >= int(mPoints.size()))
        return;
    EnemySpawnState& s = mStates[size_t(index)];
    s.armed = true;
    s.exhausted = false;
    s.timer = 0.0f;
    emit(ctx, enemies, index, mPoints[size_t(index)].count);
    ++s.wavesSpawned;
    s.spawnedTotal += mPoints[size_t(index)].count;
}

void EnemySpawner::update(GameContext& ctx, EnemySystem& enemies,
                          glm::vec3 playerFeet, float dt)
{
    if (mPoints.empty())
        return;

    // One view over the enemy archetype attributes each live enemy to its
    // spawner, so the pacing rules below cost nothing per spawner.
    std::vector<int> alive(mPoints.size(), 0);
    for (auto [e, origin, render] :
         enemies.registry().view<const EnemyOrigin, const EnemyRender>().each()) {
        if (render.dying || origin.spawnerIndex < 0 ||
            origin.spawnerIndex >= int(alive.size()))
            continue;
        ++alive[size_t(origin.spawnerIndex)];
    }

    for (size_t i = 0; i < mPoints.size(); ++i) {
        const float distance = glm::length(mPoints[i].position - playerFeet);
        const SpawnDecision d = tickSpawner(mPoints[i], mStates[i], distance,
                                            alive[i], dt);
        if (d.spawnCount > 0)
            emit(ctx, enemies, int(i), d.spawnCount);
    }
}

} // namespace game
