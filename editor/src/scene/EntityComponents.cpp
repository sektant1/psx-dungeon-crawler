#include <editor/scene/EntityComponents.h>

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

} // namespace

const char* gameplayName(Gameplay kind)
{
    switch (kind) {
    case Gameplay::Group:            return "group";
    case Gameplay::PlayerSpawn:      return "player spawn";
    case Gameplay::Portal:           return "portal";
    case Gameplay::Exit:             return "exit marker";
    case Gameplay::Marker:           return "marker";
    case Gameplay::EnemySpawn:       return "enemy spawn";
    case Gameplay::Pickup:           return "pickup";
    case Gameplay::Trigger:          return "trigger volume";
    case Gameplay::AudioEmitter:     return "audio emitter";
    case Gameplay::PointLight:       return "point light";
    case Gameplay::DirectionalLight: return "directional light";
    }
    return "entity";
}

bool gameplayIsPaintable(Gameplay kind)
{
    return kind != Gameplay::DirectionalLight;
}

const std::vector<Gameplay>& paintableGameplay()
{
    static const std::vector<Gameplay> kKinds = {
        Gameplay::Group,     Gameplay::PlayerSpawn, Gameplay::Portal,
        Gameplay::Exit,      Gameplay::Marker,      Gameplay::EnemySpawn,
        Gameplay::Pickup,    Gameplay::Trigger,     Gameplay::AudioEmitter,
        Gameplay::PointLight,
    };
    return kKinds;
}

namespace {

// The defaults a freshly added component gets. They are the values the editor
// used to hardcode inside addGameplayEntity; keeping them here is what makes
// "add a light to this wall" and "create a light entity" produce the same
// thing.
const std::vector<ComponentType>& table()
{
    static const std::vector<ComponentType> kTypes = {
        // Three ways to be geometry, in the order an author reaches for them:
        // a kit piece is the level's own vocabulary, a mesh file is anything
        // else in the project, a primitive is generated on the spot. Each `has`
        // is exclusive of the other two, so the add menu offers whichever the
        // entity is not already, and a click can never produce an entity
        // carrying two -- which the .scn format refuses to load.
        {"prefab", "Kit Piece", "a piece of the modular kit, from kit.toml",
         [](const Entity& e) { return !e.prefab.empty(); },
         [](Entity& e, const ComponentDefaults& d) { e.prefab = d.prefab; },
         [](Entity& e) {
             e.prefab.clear();
             e.material.clear();
             e.cell.reset(); // a grid cell without a piece means nothing
         },
         [](const ComponentDefaults& d) { return !d.prefab.empty(); },
         ComponentGroup::Appearance,
         [](const Entity& e) { return !e.mesh && !e.primitive; }},

        {"mesh", "Mesh", "any mesh file in the project, chosen from Meshes",
         [](const Entity& e) { return e.mesh.has_value(); },
         [](Entity& e, const ComponentDefaults& d) {
             e.mesh = game::content::MeshAuthor{d.meshPath, 1.0f};
         },
         [](Entity& e) {
             e.mesh.reset();
             e.material.clear();
         },
         // A path is not invented here for the same reason a prefab is not: the
         // catalogue knows what exists and this table does not. The Meshes tab
         // arms the brush, exactly as Placeables arms it for a kit piece.
         [](const ComponentDefaults& d) { return !d.meshPath.empty(); },
         ComponentGroup::Appearance,
         [](const Entity& e) { return e.prefab.empty() && !e.primitive; }},

        {"primitive", "Primitive Mesh",
         "a box, sphere or capsule the engine generates",
         [](const Entity& e) { return e.primitive.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             // The unit box, which is what every greybox starts as and the one
             // primitive whose defaults need no explanation.
             e.primitive = game::content::PrimitiveAuthor{};
         },
         [](Entity& e) {
             e.primitive.reset();
             e.material.clear();
         },
         always, ComponentGroup::Appearance,
         [](const Entity& e) { return e.prefab.empty() && !e.mesh; }},

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
                 LightAuthor::Type::Point, {1.0f, 0.75f, 0.45f}, 8.0f, false,
                 std::nullopt};
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

        {"portal", "Portal", "animated portal surface parameters",
         [](const Entity& e) { return e.portal.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             e.portal = game::content::PortalAuthor{};
         },
         [](Entity& e) { e.portal.reset(); }, always,
         ComponentGroup::Appearance},

        {"camera", "Camera", "a point of view the scene can be played from",
         [](const Entity& e) { return e.camera.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             e.camera = game::content::CameraAuthor{};
         },
         [](Entity& e) { e.camera.reset(); }, always,
         ComponentGroup::Gameplay},

        // The player rig. Both belong on the camera the player looks through,
        // which is why they sit directly after it in the table: an author who
        // has just added a camera and means it to be the player's eye finds
        // the next two rows already under the cursor.
        {"first_person", "First-Person Controller",
         "how the player moves and what the lens does",
         [](const Entity& e) { return e.firstPerson.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             e.firstPerson = game::content::FirstPersonAuthor{};
         },
         [](Entity& e) { e.firstPerson.reset(); }, always,
         ComponentGroup::Gameplay},

        {"viewmodel_rig", "Viewmodel Rig",
         "where the first-person hands sit and how they move",
         [](const Entity& e) { return e.viewmodelRig.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             e.viewmodelRig = game::content::ViewmodelRigAuthor{};
         },
         [](Entity& e) { e.viewmodelRig.reset(); }, always,
         ComponentGroup::Gameplay},

        {"audio", "Audio Emitter", "a clip emitted from this entity",
         [](const Entity& e) { return e.audio.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             e.audio = game::content::AudioEmitterAuthor{};
         },
         [](Entity& e) { e.audio.reset(); }, always,
         ComponentGroup::Gameplay},

        {"audio_listener", "Audio Listener",
         "candidate ears attached to this entity",
         [](const Entity& e) { return e.audioListener.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             e.audioListener = game::content::AudioListenerAuthor{};
         },
         [](Entity& e) { e.audioListener.reset(); }, always,
         ComponentGroup::Gameplay},

        {"actor", "Actor", "this entity is a player, an NPC or an enemy",
         [](const Entity& e) { return e.actor.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             // NPC by default: the other two kinds are already implied by the
             // spawn components, so the one an author has to state is this one.
             e.actor = game::ActorKind::Npc;
         },
         [](Entity& e) { e.actor.reset(); }, always,
         ComponentGroup::Gameplay},

        {"sounds", "Sounds", "a cue per action this actor can perform",
         [](const Entity& e) { return e.sounds.has_value(); },
         [](Entity& e, const ComponentDefaults&) {
             // Every field empty: an actor added to the table sounds exactly as
             // its type says it does until an author overrides a row. Adding a
             // component must never change what the game does before anything
             // is typed into it.
             e.sounds = game::content::ActorSoundsAuthor{};
         },
         [](Entity& e) { e.sounds.reset(); }, always,
         ComponentGroup::Gameplay,
         [](const Entity& e) { return isActor(e); }},

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
        if (type.add && !type.has(entity) &&
            (!type.applies || type.applies(entity)))
            out.push_back(&type);
    std::stable_sort(out.begin(), out.end(),
                     [](const ComponentType* a, const ComponentType* b) {
                         return int(a->group) < int(b->group);
                     });
    return out;
}

ComponentPresence
componentPresence(const ComponentType& type,
                  const std::vector<const Entity*>& entities)
{
    ComponentPresence presence;
    presence.total = entities.size();
    for (const Entity* entity : entities)
        if (entity && type.has(*entity))
            ++presence.present;
    return presence;
}

std::vector<const ComponentType*>
missingComponents(const std::vector<const Entity*>& entities)
{
    std::vector<const ComponentType*> out;
    if (entities.empty())
        return out;
    for (const ComponentType& type : table()) {
        if (!type.add || componentPresence(type, entities).all())
            continue;
        // Offered when it applies to at least one of the selection: adding
        // Sounds to a mixed selection of enemies and walls should give the
        // enemies a table rather than being greyed out by the walls.
        if (type.applies) {
            bool any = false;
            for (const Entity* entity : entities)
                any = any || (entity && type.applies(*entity));
            if (!any)
                continue;
        }
        out.push_back(&type);
    }
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
        if (id == "prefab" || id == "cell" || id == "collider")
            continue; // a wall with a collider is still a wall
        if (type.has(entity))
            return false;
    }
    return true;
}

} // namespace ed
