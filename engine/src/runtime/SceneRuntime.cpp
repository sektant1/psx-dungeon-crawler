#include <eng/runtime/SceneRuntime.h>

#include <eng/Log.h>
#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/MapSerializer.h>
#include <eng/ecs/components/MeshSource.h>

namespace eng::runtime {

SceneRuntime::SceneRuntime(ecs::World& world, uint32_t group,
                           const ecs::ComponentRegistry& components)
    : mWorld(world), mComponents(components), mGroup(group)
{
    // Spawn groups start above the scene's, so the first instantiate() cannot
    // collide with the level's own group however the level was numbered.
    mLastSpawnGroup = group + 1000;
}

bool SceneRuntime::load(const std::string& path)
{
    // Parse into a scratch registry first, then merge. Reading straight into
    // the live world would leave a half-decoded map behind on a malformed
    // file, and -- the world being shared -- would throw away every entity the
    // rest of the application had already spawned.
    entt::registry parsed;
    if (!eng::ecs::readMap(path, parsed, mComponents))
        return false;

    const std::size_t added = ecs::copyEntities(mWorld.registry(), parsed,
                                                mComponents, mGroup);
    log::info("Scene: '%s' merged %zu entities", path.c_str(), added);
    return true;
}

void SceneRuntime::resolveMeshes(const LoadMeshFn& loadFn)
{
    entt::registry& reg = mWorld.registry();
    for (const entt::entity e : reg.view<ecs::MeshRenderer>()) {
        if (const auto* src = reg.try_get<ecs::MeshSource>(e))
            reg.get<ecs::MeshRenderer>(e).mesh = loadFn(src->path);
    }
}

void SceneRuntime::resolvePrimitives(Renderer& renderer)
{
    ecs::resolvePrimitiveMeshes(mWorld.registry(), renderer, mPrimitives);
}

uint32_t SceneRuntime::instantiate(const std::string& path,
                                   const glm::vec3& origin)
{
    entt::registry parsed;
    if (!eng::ecs::readMap(path, parsed, mComponents)) {
        log::error("Scene: instantiate failed: %s", path.c_str());
        return 0;
    }

    // Offset the roots only. A child's transform is relative to its parent, so
    // moving both would move it twice -- and the parent link survives the copy,
    // which is what makes a spawned scene arrive as one object rather than as
    // a handful of pieces at the origin.
    for (const entt::entity e : parsed.view<ecs::Transform>()) {
        if (parsed.all_of<ecs::Parent>(e))
            continue;
        parsed.get<ecs::Transform>(e).position += origin;
    }

    // Its own group, so despawning it is one call and cannot take the level
    // with it. Counted up from the scene's, which is what keeps the two apart
    // for the life of the world.
    const uint32_t group = ++mLastSpawnGroup;
    const std::size_t added =
        ecs::copyEntities(mWorld.registry(), parsed, mComponents, group);
    log::info("Scene: instantiated '%s' -- %zu entities in group %u",
              path.c_str(), added, group);
    return group;
}

void SceneRuntime::resolveNewPrimitives(Renderer& renderer)
{
    ecs::resolvePrimitiveMeshes(mWorld.registry(), renderer, mPrimitives,
                                /*onlyUnresolved=*/true);
}

void SceneRuntime::buildAll(const BuildHook& before)
{
    if (before)
        before(mWorld.registry());
    mWorld.sync();
}

glm::vec3 SceneRuntime::playerSpawn() const
{
    const entt::registry& reg = mWorld.registry();
    for (const entt::entity e : reg.view<const ecs::FirstPersonController>()) {
        // `active` is how an author parks a tuning without deleting it, so an
        // inactive rig is not a spawn point either.
        if (!reg.get<const ecs::FirstPersonController>(e).active)
            continue;
        // The world transform when there is one: a rig parented under a room
        // is authored at a local offset, and standing the player at that
        // offset from the origin puts them through a wall.
        if (const auto* world = reg.try_get<ecs::WorldTransform>(e))
            return glm::vec3(world->matrix[3]);
        if (const auto* t = reg.try_get<ecs::Transform>(e))
            return t->position;
    }
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

SceneRuntime::AuthoredRig SceneRuntime::rig() const
{
    AuthoredRig rig;
    const entt::registry& reg = mWorld.registry();
    // First of each, independently: the components usually ride the same
    // camera, but nothing forces that, and a scene that puts the rig on its
    // spawn instead should still be read.
    for (const entt::entity e : reg.view<const ecs::FirstPersonController>()) {
        const auto& authored = reg.get<const ecs::FirstPersonController>(e);
        if (!authored.active)
            continue;
        rig.controller = authored;
        break;
    }
    for (const entt::entity e : reg.view<const ecs::ThirdPersonCamera>()) {
        const auto& authored = reg.get<const ecs::ThirdPersonCamera>(e);
        if (!authored.active)
            continue;
        rig.thirdPerson = authored;
        break;
    }
    for (const entt::entity e : reg.view<const ecs::ScreenCamera>()) {
        const auto& authored = reg.get<const ecs::ScreenCamera>(e);
        if (!authored.active)
            continue;
        rig.screen = authored;
        break;
    }
    return rig;
}

bool SceneRuntime::hasAuthoredCamera() const
{
    return !mWorld.registry().view<const ecs::Camera>().empty();
}

bool mapHasCamera(const std::string& path,
                  const ecs::ComponentRegistry& components)
{
    entt::registry parsed;
    if (!eng::ecs::readMap(path, parsed, components))
        return false;
    return !parsed.view<ecs::Camera>().empty();
}

} // namespace eng::runtime
