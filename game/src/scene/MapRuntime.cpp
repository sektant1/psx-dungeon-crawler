#include "MapRuntime.h"


#include <cmath>

#include "ComponentRegistry.h"
#include "GameComponents.h"

#include <eng/Log.h>
#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/Components.h>

namespace game {

namespace {
// A Trigger is a game concept -- an event region -- and what it means
// physically is this game's decision: a non-blocking sensor body on the
// trigger layer, so props and projectiles cannot fire it (see GameCollision.h).
// Expressing that here rather than inside the engine's PhysicsSync is what
// lets the engine stay ignorant of the layer taxonomy, and it is why
// SceneRuntime::buildAll takes a hook rather than doing this itself.
void materialiseTriggers(entt::registry& reg)
{
    for (auto e : reg.view<Trigger>(entt::exclude<Collider>)) {
        const Trigger& t = reg.get<Trigger>(e);
        reg.emplace<Collider>(e, Collider{t.shape, t.size, layer::Trigger,
                                          /*sensor=*/true});
    }
}

// The cue overrides authored on a placement, or an empty set. Every placement
// query reports them, so a caller never has to know whether the encounter came
// from a marker or from an EnemySpawn component to honour them.
ActorSoundSet authoredSounds(const entt::registry& reg, entt::entity entity)
{
    if (const ActorSounds* sounds = reg.try_get<ActorSounds>(entity))
        return sounds->set;
    return {};
}
} // namespace

MapRuntime::MapRuntime(eng::ecs::World& world, uint32_t group)
    : mScene(world, group, mapio::coreRegistry())
{
}

bool MapRuntime::load(const std::string& path)
{
    return mScene.load(path);
}

void MapRuntime::resolveMeshes(const LoadMeshFn& loadFn)
{
    mScene.resolveMeshes(loadFn);
}

void MapRuntime::resolvePrimitives(eng::Renderer& renderer)
{
    mScene.resolvePrimitives(renderer);
}

void MapRuntime::buildAll()
{
    mScene.buildAll(&materialiseTriggers);
}

glm::vec3 MapRuntime::playerSpawn() const
{
    const entt::registry& reg = mScene.registry();
    auto view = reg.view<const PlayerSpawn>();
    for (auto e : view) {
        if (const auto* world = reg.try_get<eng::ecs::WorldTransform>(e))
            return glm::vec3(world->matrix[3]);
        if (const auto* t = reg.try_get<eng::ecs::Transform>(e))
            return t->position;
    }
    // No authored marker: fall through to what the engine reads, which is the
    // authored first-person rig and then just above the origin.
    return mScene.playerSpawn();
}

float MapRuntime::playerSpawnYaw() const
{
    const entt::registry& reg = mScene.registry();
    for (auto e : reg.view<const PlayerSpawn>()) {
        const auto* transform = reg.try_get<eng::ecs::Transform>(e);
        if (!transform)
            continue;
        // Yaw out of the quaternion rather than the composed world matrix: a
        // spawn is not parented to anything that turns, and extracting an angle
        // from a matrix that may also carry scale is a way to get a subtly
        // wrong one.
        const glm::vec3 forward =
            transform->rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        if (forward.x * forward.x + forward.z * forward.z < 1e-6f)
            continue; // pointing straight up or down: no yaw to speak of
        return std::atan2(-forward.x, -forward.z);
    }
    return 0.0f;
}

glm::vec3 MapRuntime::levelExit() const
{
    const entt::registry& reg = mScene.registry();
    for (const auto entity : reg.view<const Exit>()) {
        if (const auto* transform = reg.try_get<eng::ecs::Transform>(entity))
            return transform->position;
    }
    return glm::vec3(0.0f);
}

float MapRuntime::exitYawDegrees() const
{
    const entt::registry& reg = mScene.registry();
    for (const auto entity : reg.view<const Exit>())
        return reg.get<const Exit>(entity).yawDegrees;
    return 0.0f;
}

std::vector<ScenePlacement> MapRuntime::placements(
    const std::string& prefix) const
{
    std::vector<ScenePlacement> result;
    const entt::registry& reg = mScene.registry();
    for (const auto entity : reg.view<const SceneMarker,
                                      const eng::ecs::Transform>()) {
        const auto& marker = reg.get<const SceneMarker>(entity);
        if (!prefix.empty() && marker.type.rfind(prefix, 0) != 0)
            continue;
        const auto& transform = reg.get<const eng::ecs::Transform>(entity);
        result.push_back({marker.type, transform.position, transform.rotation,
                          authoredSounds(reg, entity)});
    }
    return result;
}

namespace {

// Shared shape of the component-to-placement queries below.
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
        result.push_back({prefix + id, transform.position, transform.rotation,
                          authoredSounds(reg, entity)});
    }
    return result;
}

} // namespace

std::vector<ScenePlacement> MapRuntime::enemySpawnPlacements() const
{
    return componentPlacements<EnemySpawn>(
        mScene.registry(), "enemy.",
        [](const EnemySpawn& spawn) -> const std::string& { return spawn.type; });
}

std::string MapRuntime::palette() const
{
    // The first one found. The cooker writes at most one, and a map that
    // somehow carries two has a level design problem, not a runtime one.
    const entt::registry& reg = mScene.registry();
    for (const auto entity : reg.view<const SceneEnvironment>())
        return reg.get<const SceneEnvironment>(entity).palette;
    return {};
}

MapRuntime::AuthoredPlayerRig MapRuntime::playerRig() const
{
    // The three engine components come from the engine's own reading of them;
    // duplicating that here would be a second answer to the same question.
    const eng::runtime::SceneRuntime::AuthoredRig base = mScene.rig();
    AuthoredPlayerRig rig;
    rig.controller = base.controller;
    rig.thirdPerson = base.thirdPerson;
    rig.screen = base.screen;
    for (const auto entity : mScene.registry().view<const ViewmodelRig>()) {
        rig.viewmodel = mScene.registry().get<const ViewmodelRig>(entity);
        break;
    }
    return rig;
}

std::vector<ScenePlacement> MapRuntime::pickupPlacements() const
{
    return componentPlacements<Pickup>(
        mScene.registry(), "pickup.",
        [](const Pickup& pickup) -> const std::string& { return pickup.type; });
}

std::vector<ScenePlacement> MapRuntime::npcPlacements() const
{
    return componentPlacements<Npc>(
        mScene.registry(), "npc.",
        [](const Npc& npc) -> const std::string& { return npc.id; });
}

} // namespace game
