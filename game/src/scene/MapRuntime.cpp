#include "MapRuntime.h"

#include "GameComponents.h"
#include "MapSerializer.h"

#include <eng/Log.h>
#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/components/MeshSource.h>

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

MapRuntime::MapRuntime(eng::ecs::World& world, uint32_t group)
    : mWorld(world), mGroup(group)
{
}

bool MapRuntime::load(const std::string& path)
{
    // Parse into a scratch registry first, then merge. Reading straight into
    // the live world would leave a half-decoded map behind on a malformed file,
    // and -- now that the world is shared -- the old `registry = std::move(...)`
    // would have thrown away every entity the rest of the game had spawned.
    entt::registry parsed;
    if (!mapio::readMap(path, parsed, mapio::coreRegistry()))
        return false;

    entt::registry& reg = mWorld.registry();
    // copyEntities tags each copy Dirty; this used to dirty every entity in the
    // world instead, which also re-resolved the player and everything the last
    // level left standing.
    const std::size_t added =
        eng::ecs::copyEntities(reg, parsed, mapio::coreRegistry(), mGroup);
    eng::log::info("Map: '%s' merged %zu entities", path.c_str(), added);
    return true;
}

void MapRuntime::resolveMeshes(const LoadMeshFn& loadFn)
{
    entt::registry& reg = mWorld.registry();
    auto view = reg.view<eng::ecs::MeshRenderer>();
    for (auto e : view) {
        if (const auto* src = reg.try_get<eng::ecs::MeshSource>(e)) {
            reg.get<eng::ecs::MeshRenderer>(e).mesh = loadFn(src->path);
        }
    }
}

void MapRuntime::buildAll()
{
    materialiseTriggers(mWorld.registry());
    mWorld.sync();
}

glm::vec3 MapRuntime::playerSpawn() const
{
    const entt::registry& reg = mWorld.registry();
    auto view = reg.view<const PlayerSpawn>();
    for (auto e : view) {
        if (const auto* world = reg.try_get<eng::ecs::WorldTransform>(e))
            return glm::vec3(world->matrix[3]);
        if (const auto* t = reg.try_get<eng::ecs::Transform>(e))
            return t->position;
    }
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec3 MapRuntime::levelExit() const
{
    const entt::registry& reg = mWorld.registry();
    for (const auto entity : reg.view<const Exit>()) {
        if (const auto* transform = reg.try_get<eng::ecs::Transform>(entity))
            return transform->position;
    }
    return glm::vec3(0.0f);
}

float MapRuntime::exitYawDegrees() const
{
    const entt::registry& reg = mWorld.registry();
    for (const auto entity : reg.view<const Exit>())
        return reg.get<const Exit>(entity).yawDegrees;
    return 0.0f;
}

std::vector<ScenePlacement> MapRuntime::placements(
    const std::string& prefix) const
{
    std::vector<ScenePlacement> result;
    const entt::registry& reg = mWorld.registry();
    for (const auto entity : reg.view<const SceneMarker,
                                      const eng::ecs::Transform>()) {
        const auto& marker = reg.get<const SceneMarker>(entity);
        if (!prefix.empty() && marker.type.rfind(prefix, 0) != 0)
            continue;
        const auto& transform = reg.get<const eng::ecs::Transform>(entity);
        result.push_back({marker.type, transform.position, transform.rotation});
    }
    return result;
}

namespace {

// Shared shape of the two component-to-placement queries below.
template <typename Component, typename Name>
std::vector<ScenePlacement> componentPlacements(const entt::registry& reg,
                                                const char* prefix, Name name)
{
    std::vector<ScenePlacement> result;
    for (const auto entity : reg.view<const Component,
                                      const eng::ecs::Transform>()) {
        const std::string& id = name(reg.get<const Component>(entity));
        if (id.empty())
            continue;
        const auto& transform = reg.get<const eng::ecs::Transform>(entity);
        result.push_back(
            {prefix + id, transform.position, transform.rotation});
    }
    return result;
}

} // namespace

std::vector<ScenePlacement> MapRuntime::enemySpawnPlacements() const
{
    return componentPlacements<EnemySpawn>(
        mWorld.registry(), "enemy.",
        [](const EnemySpawn& spawn) -> const std::string& { return spawn.type; });
}

std::string MapRuntime::palette() const
{
    // The first one found. The cooker writes at most one, and a map that
    // somehow carries two has a level design problem, not a runtime one.
    for (const auto entity : mWorld.registry().view<const SceneEnvironment>())
        return mWorld.registry().get<const SceneEnvironment>(entity).palette;
    return {};
}

std::vector<ScenePlacement> MapRuntime::pickupPlacements() const
{
    return componentPlacements<Pickup>(
        mWorld.registry(), "pickup.",
        [](const Pickup& pickup) -> const std::string& { return pickup.type; });
}

} // namespace game
