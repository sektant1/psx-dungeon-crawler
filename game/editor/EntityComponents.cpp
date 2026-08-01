#include "EntityComponents.h"

#include <algorithm>

namespace ed {
namespace {

using game::content::Entity;
using game::content::LightAuthor;
using game::content::TriggerAuthor;

bool always(const ComponentDefaults&)
{
    return true;
}

// The defaults a freshly added component gets. They are the values the editor
// used to hardcode inside addGameplayEntity; keeping them here is what makes
// "add a light to this wall" and "create a light entity" produce the same
// thing.
const std::vector<ComponentType>& table()
{
    static const std::vector<ComponentType> kTypes = {
        {"mesh", "Mesh", "kit piece drawn in the world",
         [](const Entity& e) { return !e.prefab.empty(); },
         [](Entity& e, const ComponentDefaults& d) { e.prefab = d.prefab; },
         [](Entity& e) {
             e.prefab.clear();
             e.material.clear();
             e.cell.reset(); // a grid cell without a piece means nothing
         },
         [](const ComponentDefaults& d) { return !d.prefab.empty(); },
         ComponentGroup::Appearance},

        {"cell", "Grid Cell", "pinned to a cell or edge of the level grid",
         [](const Entity& e) { return e.cell.has_value(); }, nullptr,
         [](Entity& e) { e.cell.reset(); }, nullptr,
         ComponentGroup::Placement},

        {"collider", "Collider", "box the player and projectiles hit",
         [](const Entity& e) { return e.collider.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             e.collider = game::content::ColliderAuthor{};
         },
         [](Entity& e) { e.collider.reset(); }, always,
         ComponentGroup::Physical},

        {"light", "Light", "point or directional light",
         [](const Entity& e) { return e.light.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             e.light = LightAuthor{
                 LightAuthor::Type::Point, {1.0f, 0.75f, 0.45f}, 8.0f, false};
         },
         [](Entity& e) { e.light.reset(); }, always,
         ComponentGroup::Appearance},

        {"shader", "Shader", "per-entity tint, rim light and cutout",
         [](const Entity& e) { return e.shader.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             // Added at the engine defaults, which render exactly as the entity
             // did without it. Adding a component must never change what is on
             // screen before the author touches a slider -- otherwise every
             // "what does this do" costs an undo.
             e.shader = game::content::ShaderAuthor{};
         },
         [](Entity& e) { e.shader.reset(); }, always,
         ComponentGroup::Appearance},

        {"particles", "Particles", "an effect playing from this entity",
         [](const Entity& e) { return e.particles.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             // Empty effect name: added, and playing nothing until the author
             // picks one. Spawning a default effect would put something in the
             // level that nobody asked for.
             e.particles = game::content::ParticleAuthor{};
         },
         [](Entity& e) { e.particles.reset(); }, always,
         ComponentGroup::Appearance},

        {"camera", "Camera", "a point of view the scene can be played from",
         [](const Entity& e) { return e.camera.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             e.camera = game::content::CameraAuthor{};
         },
         [](Entity& e) { e.camera.reset(); }, always,
         ComponentGroup::Gameplay},

        {"orbit", "Orbit", "travels a ring around a point -- no pivot needed",
         [](const Entity& e) { return e.orbit.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             game::content::OrbitAuthor orbit;
             // Centred on where the entity already is, so adding the component
             // does not teleport it across the level -- it starts circling the
             // spot it was placed at, which is what the author was looking at.
             orbit.centre = e.transform.position;
             e.orbit = orbit;
         },
         [](Entity& e) { e.orbit.reset(); }, always},

        {"spin", "Spin", "turns forever -- and turns whatever hangs under it",
         [](const Entity& e) { return e.spin.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             e.spin = game::content::SpinAuthor{};
         },
         [](Entity& e) { e.spin.reset(); }, always,
         ComponentGroup::Gameplay},

        {"player_spawn", "Player Spawn", "where the player starts the level",
         [](const Entity& e) { return e.playerSpawn; },
         [](Entity& e, const ComponentDefaults&) { e.playerSpawn = true; },
         [](Entity& e) { e.playerSpawn = false; }, always,
         ComponentGroup::Gameplay},

        {"exit", "Exit", "level exit, with the yaw the player leaves facing",
         [](const Entity& e) { return e.exitYawDegrees.has_value(); },
         [](Entity& e, const ComponentDefaults&) { e.exitYawDegrees = 0.0f; },
         [](Entity& e) { e.exitYawDegrees.reset(); }, always,
         ComponentGroup::Gameplay},

        {"marker", "Marker", "named point gameplay code can look up",
         [](const Entity& e) { return e.marker.has_value(); },
         [](Entity& e, const ComponentDefaults&) { e.marker = "group.name"; },
         [](Entity& e) { e.marker.reset(); }, always,
         ComponentGroup::Gameplay},

        {"enemy_spawn", "Enemy Spawn", "spawns one enemy of a type",
         [](const Entity& e) { return e.enemySpawn.has_value(); },
         [](Entity& e, const ComponentDefaults&) { e.enemySpawn = "goblin"; },
         [](Entity& e) { e.enemySpawn.reset(); }, always,
         ComponentGroup::Gameplay},

        {"pickup", "Pickup", "item the player can take",
         [](const Entity& e) { return e.pickup.has_value(); },
         [](Entity& e, const ComponentDefaults&) { e.pickup = "potion"; },
         [](Entity& e) { e.pickup.reset(); }, always,
         ComponentGroup::Gameplay},

        {"trigger", "Trigger", "box volume that raises an event",
         [](const Entity& e) { return e.trigger.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             e.trigger = TriggerAuthor{{2.0f, 2.0f, 2.0f}, "event.name"};
         },
         [](Entity& e) { e.trigger.reset(); }, always,
         ComponentGroup::Physical},
    };
    return kTypes;
}

} // namespace

const char* componentGroupName(ComponentGroup group)
{
    switch (group) {
    case ComponentGroup::Appearance: return "appearance";
    case ComponentGroup::Physical:   return "physical";
    case ComponentGroup::Gameplay:   return "gameplay";
    case ComponentGroup::Placement:  return "placement";
    }
    return "";
}

const std::vector<ComponentType>& componentTypes()
{
    return table();
}

const ComponentType* findComponentType(std::string_view id)
{
    for (const ComponentType& type : table())
        if (id == type.id)
            return &type;
    return nullptr;
}

bool hasComponent(const Entity& entity, std::string_view id)
{
    const ComponentType* type = findComponentType(id);
    return type && type->has(entity);
}

// Sorted by group, so every entity's parts read in the same order however many
// it happens to carry. Stable, so the hand-written order inside a band is kept
// -- that one is chosen too.
std::vector<const ComponentType*> componentsOf(const Entity& entity)
{
    std::vector<const ComponentType*> out;
    for (const ComponentType& type : table())
        if (type.has(entity))
            out.push_back(&type);
    std::stable_sort(out.begin(), out.end(),
                     [](const ComponentType* a, const ComponentType* b) {
                         return int(a->group) < int(b->group);
                     });
    return out;
}

std::vector<const ComponentType*> missingComponents(const Entity& entity)
{
    std::vector<const ComponentType*> out;
    for (const ComponentType& type : table())
        if (type.add && !type.has(entity))
            out.push_back(&type);
    std::stable_sort(out.begin(), out.end(),
                     [](const ComponentType* a, const ComponentType* b) {
                         return int(a->group) < int(b->group);
                     });
    return out;
}

bool isGeometry(const Entity& entity)
{
    if (entity.prefab.empty())
        return false;
    for (const ComponentType& type : table()) {
        const std::string_view id = type.id;
        if (id == "mesh" || id == "cell" || id == "collider")
            continue; // a wall with a collider is still a wall
        if (type.has(entity))
            return false;
    }
    return true;
}

} // namespace ed
