#include <editor/content/SceneCook.h>

#include "GameComponents.h"
#include "MapSerializer.h"
#include "scene/ComponentRegistry.h"

#include <eng/ecs/Components.h>
#include <eng/ecs/components/MeshSource.h>

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

// Whether anything above this entity moves at runtime.
//
// Baking a hierarchy into world transforms is exact *while the chain is
// static*, which every authored level was until components could animate one.
// A camera hanging off a spinning pivot baked flat is a camera that does not
// orbit -- and the failure is silent, because the scene still loads and still
// draws. So a live chain keeps its links and its local transforms, and
// everything else cooks flat exactly as it did before.
// Whether anything above this entity moves at runtime -- a Spin or an Orbit.
bool ancestorAnimated(const SceneDocument& document, const Entity& entity)
{
    std::vector<std::string> seen{entity.id};
    for (const Entity* at = entity.parent.empty() ? nullptr
                                                  : document.find(entity.parent);
         at != nullptr;) {
        if (std::find(seen.begin(), seen.end(), at->id) != seen.end())
            break; // a cycle: validate() reports it, this must not hang
        seen.push_back(at->id);
        if (at->spin || at->orbit)
            return true;
        at = at->parent.empty() ? nullptr : document.find(at->parent);
    }
    return false;
}

} // namespace

bool buildRegistry(const SceneDocument& document, const KitCatalog& catalog,
                   entt::registry& out, std::string& error,
                   std::unordered_map<AuthorId, entt::entity>* authorToEntity,
                   std::vector<AuthorId>* unresolved)
{
    error.clear();
    if (authorToEntity)
        authorToEntity->clear();
    if (unresolved)
        unresolved->clear();

    // Author-id order, not document order: two documents with the same content
    // in a different order must cook to the same bytes.
    std::vector<const Entity*> ordered;
    ordered.reserve(document.entities.size());
    for (const Entity& entity : document.entities)
        ordered.push_back(&entity);
    std::sort(ordered.begin(), ordered.end(),
              [](const Entity* a, const Entity* b) { return a->id < b->id; });

    entt::registry built;
    // Parallel to `ordered`: the entity each authored id became, for the link
    // pass at the end.
    std::vector<entt::entity> ids;
    ids.reserve(ordered.size());
    for (const Entity* source : ordered) {
        const Entity& authored = *source;
        // Resolved through the parent chain here, once, and never again: the
        // runtime map is a flat list of world transforms, so hierarchies are an
        // authoring convenience the cooker spends. An entity with no parent --
        // which is every entity in every scene authored before hierarchies
        // existed -- resolves to its own transform, byte for byte.
        const WorldTransform world = document.worldTransform(authored.id);
        // ...unless something above it moves, in which case the link is the
        // point and the transform stays local (see ancestorAnimated).
        const bool live = ancestorAnimated(document, authored);
        eng::ecs::Transform transform;
        transform.position = live ? authored.transform.position : world.position;
        transform.rotation = live ? authorOrientation(authored.transform.rotationDegrees)
                                  : world.orientation;
        transform.scale = live ? authored.transform.scale : world.scale;

        const entt::entity entity = built.create();
        ids.push_back(entity);
        if (authorToEntity)
            authorToEntity->emplace(authored.id, entity);
        built.emplace<eng::ecs::Name>(
            entity,
            eng::ecs::Name{authored.name.empty() ? authored.id : authored.name});

        const KitPiece* piece =
            authored.prefab.empty() ? nullptr : catalog.find(authored.prefab);
        if (!authored.prefab.empty() && !piece && !unresolved) {
            error = "entity '" + authored.id + "' has unresolved prefab '" +
                    authored.prefab + "'";
            return false;
        }
        if (!authored.prefab.empty() && !piece) {
            // Recorded and carried past, for the preview. The entity is kept
            // rather than dropped: destroying it here would leave a dangling
            // handle in `ids`, which the parent-link pass below indexes by
            // position -- and the author still needs the row in the outliner to
            // select the thing and fix its prefab.
            unresolved->push_back(authored.id);
        }
        else if (!authored.prefab.empty()) {
            // The kit is authored on a 20-unit grid and imported at `scale`;
            // the authored scale multiplies that rather than replacing it.
            transform.scale *= piece->meshScale(catalog.scale());
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
            if (const auto& animation = authored.light->animation) {
                eng::ecs::LightAnimation out;
                switch (animation->mode) {
                case LightAnimAuthor::Mode::Steady:
                    out.mode = eng::ecs::LightAnimation::Steady;
                    break;
                case LightAnimAuthor::Mode::Flicker:
                    out.mode = eng::ecs::LightAnimation::Flicker;
                    break;
                case LightAnimAuthor::Mode::Pulse:
                    out.mode = eng::ecs::LightAnimation::Pulse;
                    break;
                }
                out.speed = animation->speed;
                out.amount = animation->amount;
                out.phase = animation->phase;
                built.emplace<eng::ecs::LightAnimation>(entity, out);
            }
        }

        if (authored.particles)
            built.emplace<eng::ecs::ParticleEmitter>(entity,
                                                     *authored.particles);
        if (authored.audio)
            built.emplace<eng::ecs::AudioEmitter>(entity, *authored.audio);
        if (authored.audioListener)
            built.emplace<eng::ecs::AudioListener>(entity,
                                                   *authored.audioListener);
        // The kind is resolved, not copied: a scene that says "enemy spawn" and
        // nothing else still cooks an Actor, so the runtime has one question to
        // ask instead of the three the format grew.
        if (const std::optional<game::ActorKind> kind = actorKindOf(authored))
            built.emplace<game::Actor>(entity, game::Actor{*kind});
        // Dropped on a non-actor: validate() warns, and cooking it anyway would
        // put a table in the map that nothing can play.
        if (authored.sounds && !authored.sounds->empty() && isActor(authored))
            built.emplace<game::ActorSounds>(entity,
                                             game::ActorSounds{*authored.sounds});
        if (authored.portal)
            built.emplace<eng::ecs::PortalParams>(entity, *authored.portal);
        if (authored.shader) {
            const game::content::ShaderAuthor& sh = *authored.shader;
            built.emplace<eng::ecs::ShaderParams>(
                entity, eng::ecs::ShaderParams{sh.tint, sh.opacity,
                                               sh.rimColour, sh.rimStrength,
                                               sh.rimPower, sh.alphaScissor});
        }
        if (authored.camera) {
            built.emplace<eng::ecs::Camera>(
                entity, eng::ecs::Camera{authored.camera->fovDegrees,
                                         authored.camera->nearClip,
                                         authored.camera->farClip,
                                         authored.camera->priority,
                                         authored.camera->active});
        }
        if (authored.orbit) {
            eng::ecs::Orbit orbit;
            orbit.centre = authored.orbit->centre;
            orbit.axis = authored.orbit->axis;
            orbit.radius = authored.orbit->radius;
            orbit.degreesPerSecond = authored.orbit->degreesPerSecond;
            orbit.phaseDegrees = authored.orbit->phaseDegrees;
            orbit.height = authored.orbit->height;
            switch (authored.orbit->facing) {
            case OrbitAuthor::Facing::Free:
                orbit.facing = eng::ecs::Orbit::Free;
                break;
            case OrbitAuthor::Facing::Centre:
                orbit.facing = eng::ecs::Orbit::Centre;
                break;
            case OrbitAuthor::Facing::Travel:
                orbit.facing = eng::ecs::Orbit::Travel;
                break;
            }
            built.emplace<eng::ecs::Orbit>(entity, orbit);
        }
        if (authored.spin) {
            built.emplace<eng::ecs::Spin>(
                entity, eng::ecs::Spin{authored.spin->axis,
                                       authored.spin->degreesPerSecond});
        }

        // Compound prefabs own their attached parts. Scenes author only the
        // root prefab; attachments stay local to it and therefore follow live
        // parent chains, authored scale, and animation without extra entities
        // in every source scene.
        if (piece) {
            const auto emitAttachments = [&](auto&& self, entt::entity parent,
                                             const KitPiece& parentPiece,
                                             const std::string& namePrefix)
                -> void {
                for (std::size_t index = 0;
                     index < parentPiece.attachments.size(); ++index) {
                    const KitAttachment& attachment =
                        parentPiece.attachments[index];
                    const KitPiece* attached = catalog.find(attachment.prefab);
                    if (!attached)
                        continue; // KitCatalog::load rejects this first.

                    const entt::entity child = built.create();
                    const std::string childName =
                        namePrefix + ".attachment_" + std::to_string(index + 1);
                    built.emplace<eng::ecs::Name>(child,
                                                  eng::ecs::Name{childName});
                    eng::ecs::Transform childTransform;
                    childTransform.position = attachment.position;
                    childTransform.scale *= attached->meshScale(catalog.scale());
                    built.emplace<eng::ecs::Transform>(child, childTransform);
                    built.emplace<eng::ecs::MeshSource>(
                        child, eng::ecs::MeshSource{attached->meshPath});
                    eng::ecs::MeshRenderer childRenderer;
                    childRenderer.material = attached->material;
                    childRenderer.castShadows = authored.castShadows;
                    built.emplace<eng::ecs::MeshRenderer>(child,
                                                          childRenderer);
                    if (authored.shader) {
                        const ShaderAuthor& shader = *authored.shader;
                        built.emplace<eng::ecs::ShaderParams>(
                            child,
                            eng::ecs::ShaderParams{
                                shader.tint, shader.opacity, shader.rimColour,
                                shader.rimStrength, shader.rimPower,
                                shader.alphaScissor});
                    }
                    built.emplace<eng::ecs::Parent>(
                        child, eng::ecs::Parent{parent});
                    built.get_or_emplace<eng::ecs::Children>(parent)
                        .value.push_back(child);
                    self(self, child, *attached, childName);
                }
            };
            emitAttachments(emitAttachments, entity, *piece, authored.id);
        }

        // A collider is a child entity rather than a component on the visual:
        // the offset would otherwise have nowhere to live, and the physics body
        // must not inherit the mesh's kit scale.
        const auto emitCollider = [&](glm::vec3 halfExtents, glm::vec3 offset) {
            const entt::entity collider = built.create();
            built.emplace<eng::ecs::Name>(
                collider, eng::ecs::Name{authored.id + ".collision"});
            eng::ecs::Transform colliderTransform;
            // The visual's world transform, not its authored one: a torch
            // parented to a wall must collide where it is drawn.
            colliderTransform.position =
                world.position + world.orientation * offset;
            colliderTransform.rotation = world.orientation;
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

    // The links, once every entity exists: a child sorted before its parent has
    // nothing to point at during the first pass. Only live chains get one --
    // everything else was baked flat above, and giving it a parent as well
    // would compose the same transform twice.
    {
        std::unordered_map<AuthorId, entt::entity> byAuthor;
        for (std::size_t i = 0; i < ordered.size(); ++i)
            byAuthor.emplace(ordered[i]->id, ids[i]);
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            const Entity& authored = *ordered[i];
            if (authored.parent.empty() || !ancestorAnimated(document, authored))
                continue;
            const auto parent = byAuthor.find(authored.parent);
            if (parent == byAuthor.end())
                continue; // validate() reports a parent that is not in the file
            built.emplace<eng::ecs::Parent>(
                ids[i], eng::ecs::Parent{parent->second});
            built.get_or_emplace<eng::ecs::Children>(parent->second)
                .value.push_back(ids[i]);
        }
    }

    // The level's own look, on an entity of its own. A .map is a flat list of
    // entities, so a fact about the whole level needs a carrier; one is written
    // only when the scene states a palette, which keeps every scene authored
    // before this cook byte-identical.
    if (!document.palette.empty()) {
        const entt::entity environment = built.create();
        built.emplace<eng::ecs::Name>(environment,
                                      eng::ecs::Name{"scene.environment"});
        built.emplace<game::SceneEnvironment>(
            environment, game::SceneEnvironment{document.palette});
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
