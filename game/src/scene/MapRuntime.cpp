#include "MapRuntime.h"

#include "MapSerializer.h"
#include <eng/ecs/MeshSource.h>
#include "GameComponents.h"

#include <eng/Physics.h>
#include <eng/ecs/Components.h>

namespace game {

namespace {
// A Trigger is a game concept -- an event region -- and what it means
// physically is this game's decision: a non-blocking sensor body on the
// trigger layer, so props and projectiles cannot fire it (see GameCollision.h).
// Expressing that here rather than inside the engine's PhysicsSync is what
// lets the engine stay ignorant of the layer taxonomy.
void materialiseTriggers(entt::registry& reg)
{
    for (auto e : reg.view<Trigger>(entt::exclude<Collider>)) {
        const Trigger& t = reg.get<Trigger>(e);
        reg.emplace<Collider>(e, Collider{t.shape, t.size, layer::Trigger,
                                          /*sensor=*/true});
    }
}
} // namespace

MapRuntime::MapRuntime(eng::ecs::SceneBackend& backend, eng::Physics& physics)
    : mSceneSync(mScene, backend), mPhysicsSync(mScene.registry(), physics),
      mPhysics(physics)
{}

bool MapRuntime::load(const std::string& path)
{
    entt::registry& reg = mScene.registry();
    mPhysicsSync.clear();
    reg.clear();
    if (!mapio::readMap(path, reg, mapio::coreRegistry())) return false;
    for (auto e : reg.view<eng::ecs::Transform>())
        reg.emplace_or_replace<eng::ecs::Dirty>(e);
    return true;
}

void MapRuntime::resolveMeshes(const LoadMeshFn& loadFn)
{
    entt::registry& reg = mScene.registry();
    auto view = reg.view<eng::ecs::MeshRenderer>();
    for (auto e : view) {
        if (const auto* src = reg.try_get<eng::ecs::MeshSource>(e)) {
            reg.get<eng::ecs::MeshRenderer>(e).mesh = loadFn(src->path);
        }
    }
}

void MapRuntime::buildAll()
{
    mSceneSync.sync();
    materialiseTriggers(mScene.registry());
    mPhysicsSync.sync();
}

void MapRuntime::step(float dt)
{
    mPhysics.update(dt);
    mSceneSync.sync();
    materialiseTriggers(mScene.registry());
    mPhysicsSync.sync();
}

glm::vec3 MapRuntime::playerSpawn() const
{
    const entt::registry& reg = mScene.registry();
    auto view = reg.view<const PlayerSpawn>();
    for (auto e : view) {
        if (const auto* t = reg.try_get<eng::ecs::Transform>(e))
            return t->position;
    }
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace game
