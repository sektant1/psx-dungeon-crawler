#include "SceneCook.h"

#include "GameComponents.h"
#include "MapSerializer.h"
#include "scene/ComponentRegistry.h"

#include <eng/ecs/Components.h>
#include <eng/ecs/MeshSource.h>

#include <algorithm>
#include <cmath>

namespace game::content {
namespace {

// The collision a kit piece carries by virtue of what it IS, derived from its
// socket and its authored size. Returns false for pieces that hold nothing up
// and stop nothing: openings, and props, which collide only where the author
// asked for it.
//
// Without this a cooked scene is architecture the player falls straight
// through: `collider` in a .scn is an optional per-entity override, and no
// authoring tool writes one for the 122 floors and walls of a room -- the
// procedural path (LayoutToScene) generates the same slabs in code, so the
// difference only showed up in scenes that came through the cooker. It is the
// piece's own kit.toml size that decides, so a thicker wall gets a thicker
// collider without a second table to keep in step.
bool implicitCollider(const KitPiece& piece, const KitCatalog& catalog,
                      glm::vec3& halfExtents, glm::vec3& offset)
{
    const glm::vec3 size = piece.sizeMeters(catalog.scale());
    switch (piece.socket) {
    case Socket::Floor: {
        // Floors are authored flat (y = 0), so the slab is given a thickness of
        // its own and hung just under the surface the mesh draws.
        const float half = catalog.cellMeters() * 0.5f;
        constexpr float slab = 0.05f;
        halfExtents = {std::max(size.x * 0.5f, half), slab,
                       std::max(size.z * 0.5f, half)};
        offset = {0.0f, -slab, 0.0f};
        return true;
    }
    case Socket::Wall:
    case Socket::Fill:
        // Base at Y=0 by the kit's convention, so the box's centre is half a
        // height up. The piece's own thickness is used rather than a fixed thin
        // slab: a wall placed by hand is not guaranteed to sit exactly on a
        // cell boundary the way a generated one is.
        halfExtents = glm::max(size * 0.5f, glm::vec3(0.05f));
        offset = {0.0f, size.y * 0.5f + piece.yOffsetMeters(catalog.scale()),
                  0.0f};
        return true;
    case Socket::Opening:
    case Socket::Prop:
        return false;
    }
    return false;
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
        transform.rotation = authorOrientation(authored.transform.rotationDegrees);
        transform.scale = authored.transform.scale;

        const entt::entity entity = built.create();
        if (authorToEntity)
            authorToEntity->emplace(authored.id, entity);
        built.emplace<eng::ecs::Name>(
            entity,
            eng::ecs::Name{authored.name.empty() ? authored.id : authored.name});

        const KitPiece* piece =
            authored.prefab.empty() ? nullptr : catalog.find(authored.prefab);
        if (!authored.prefab.empty()) {
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
            // The entity's own material wins over the kit piece's.
            renderer.material = authored.material.empty() ? piece->material
                                                          : authored.material;
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
        const auto emitCollider = [&](glm::vec3 halfExtents, glm::vec3 offset) {
            const entt::entity collider = built.create();
            built.emplace<eng::ecs::Name>(
                collider, eng::ecs::Name{authored.id + ".collision"});
            eng::ecs::Transform colliderTransform;
            colliderTransform.position =
                authored.transform.position +
                authorOrientation(authored.transform.rotationDegrees) * offset;
            colliderTransform.rotation =
                authorOrientation(authored.transform.rotationDegrees);
            built.emplace<eng::ecs::Transform>(collider, colliderTransform);
            built.emplace<game::Collider>(
                collider, game::Collider{eng::ShapeKind::Box, halfExtents,
                                         game::layer::Static, false});
        };
        if (authored.collider) {
            emitCollider(authored.collider->halfExtents,
                         authored.collider->offset);
        } else if (piece) {
            // No authored override: the piece collides as what it is. An
            // authored `collider` replaces this rather than adding to it, so a
            // scene can still make one wall passable.
            glm::vec3 halfExtents{0.0f};
            glm::vec3 offset{0.0f};
            if (implicitCollider(*piece, catalog, halfExtents, offset))
                emitCollider(halfExtents, offset);
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
