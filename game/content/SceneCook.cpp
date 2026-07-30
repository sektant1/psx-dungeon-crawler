#include "SceneCook.h"

#include "GameComponents.h"
#include "MapSerializer.h"
#include "scene/ComponentRegistry.h"

#include <eng/ecs/Components.h>
#include <eng/ecs/MeshSource.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace game::content {
namespace {

// Same convention the loader used and the runtime expects: yaw, then pitch,
// then roll.
glm::quat quatFromDegrees(const glm::vec3& degrees)
{
    return glm::angleAxis(glm::radians(degrees.y), glm::vec3(0, 1, 0)) *
           glm::angleAxis(glm::radians(degrees.x), glm::vec3(1, 0, 0)) *
           glm::angleAxis(glm::radians(degrees.z), glm::vec3(0, 0, 1));
}

} // namespace

bool buildRegistry(const SceneDocument& document, const KitCatalog& catalog,
                   entt::registry& out, std::string& error,
                   std::unordered_map<AuthorId, entt::entity>* authorToEntity)
{
    error.clear();
    if (authorToEntity)
        authorToEntity->clear();

    // Author-id order, not document order: two documents with the same content
    // in a different order must cook to the same bytes.
    std::vector<const Entity*> ordered;
    ordered.reserve(document.entities.size());
    for (const Entity& entity : document.entities)
        ordered.push_back(&entity);
    std::sort(ordered.begin(), ordered.end(),
              [](const Entity* a, const Entity* b) { return a->id < b->id; });

    entt::registry built;
    for (const Entity* source : ordered) {
        const Entity& authored = *source;
        eng::ecs::Transform transform;
        transform.position = authored.transform.position;
        transform.rotation = quatFromDegrees(authored.transform.rotationDegrees);
        transform.scale = authored.transform.scale;

        const entt::entity entity = built.create();
        if (authorToEntity)
            authorToEntity->emplace(authored.id, entity);
        built.emplace<eng::ecs::Name>(
            entity,
            eng::ecs::Name{authored.name.empty() ? authored.id : authored.name});

        if (!authored.prefab.empty()) {
            const KitPiece* piece = catalog.find(authored.prefab);
            if (!piece) {
                error = "entity '" + authored.id + "' has unresolved prefab '" +
                        authored.prefab + "'";
                return false;
            }
            // The kit is authored on a 20-unit grid and imported at `scale`;
            // the authored scale multiplies that rather than replacing it.
            transform.scale *= catalog.scale();
            built.emplace<eng::ecs::MeshSource>(
                entity, eng::ecs::MeshSource{piece->meshPath});
            eng::ecs::MeshRenderer renderer;
            renderer.material = piece->material;
            renderer.castShadows = authored.castShadows;
            built.emplace<eng::ecs::MeshRenderer>(entity, std::move(renderer));
        }
        built.emplace<eng::ecs::Transform>(entity, transform);

        if (authored.playerSpawn)
            built.emplace<game::PlayerSpawn>(entity);
        if (authored.exitYawDegrees)
            built.emplace<game::Exit>(entity, game::Exit{*authored.exitYawDegrees});
        if (authored.marker)
            built.emplace<game::SceneMarker>(entity,
                                             game::SceneMarker{*authored.marker});
        if (authored.enemySpawn)
            built.emplace<game::EnemySpawn>(entity,
                                            game::EnemySpawn{*authored.enemySpawn});
        if (authored.pickup)
            built.emplace<game::Pickup>(entity, game::Pickup{*authored.pickup});
        if (authored.trigger) {
            built.emplace<game::Trigger>(
                entity, game::Trigger{eng::ShapeKind::Box, authored.trigger->size,
                                      authored.trigger->event});
        }
        if (authored.light) {
            eng::LightDesc light;
            light.type = authored.light->type == LightAuthor::Type::Directional
                             ? eng::LightDesc::Type::Directional
                             : eng::LightDesc::Type::Point;
            light.colour = authored.light->colour;
            light.range = authored.light->range;
            light.castShadows = authored.light->castShadows;
            built.emplace<eng::ecs::LightRef>(entity,
                                              eng::ecs::LightRef{light, {}});
        }

        // A collider is a child entity rather than a component on the visual:
        // the offset would otherwise have nowhere to live, and the physics body
        // must not inherit the mesh's kit scale.
        if (authored.collider) {
            const entt::entity collider = built.create();
            built.emplace<eng::ecs::Name>(
                collider, eng::ecs::Name{authored.id + ".collision"});
            eng::ecs::Transform colliderTransform;
            colliderTransform.position =
                authored.transform.position +
                quatFromDegrees(authored.transform.rotationDegrees) *
                    authored.collider->offset;
            colliderTransform.rotation =
                quatFromDegrees(authored.transform.rotationDegrees);
            built.emplace<eng::ecs::Transform>(collider, colliderTransform);
            built.emplace<game::Collider>(
                collider,
                game::Collider{eng::ShapeKind::Box, authored.collider->halfExtents,
                               game::layer::Static, false});
        }
    }

    out = std::move(built);
    return true;
}

bool cookToMap(const SceneDocument& document, const KitCatalog& catalog,
               const std::string& mapPath, std::string& error)
{
    entt::registry registry;
    if (!buildRegistry(document, catalog, registry, error))
        return false;
    if (!mapio::writeMap(mapPath, registry, mapio::coreRegistry())) {
        error = mapPath + ": failed to write cooked map";
        return false;
    }
    return true;
}

} // namespace game::content
