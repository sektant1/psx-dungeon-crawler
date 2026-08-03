#pragma once

#include <eng/ecs/World.h>

#include "audio/ActorSounds.h"

#include <entt/entt.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace game {

struct ScenePlacement {
    std::string type;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    // Cue overrides the author put on this particular placement, empty when
    // they did not. Carried through the spawn path so "this one grunt screams
    // differently" survives the trip from the .scn to the live enemy -- the
    // authored entity and the enemy it produces are entities on two different
    // registries, so anything not carried here is lost at the seam.
    ActorSoundSet sounds;
};

// Reads a cooked .map into the level's world and answers the authored queries
// the game asks of it (where the player starts, where the exit is, what was
// placed).
//
// It does NOT own a world any more. A level has exactly one (see eng::ecs::
// World), and this used to be a second: the authored entities lived in here,
// the gameplay actors somewhere else, and the combatants in a third registry,
// so no view could see a whole level. Load merges into the world it is given.
class MapRuntime {
public:
    // `group` is stamped on every entity this map contributes, so a level
    // transition destroys exactly the map's entities and leaves the player and
    // anything else persistent alone (eng::ecs::World::destroyGroup).
    MapRuntime(eng::ecs::World& world, uint32_t group);

    // Merges the cooked map's entities into the world. Returns false and adds
    // nothing if the file is missing or malformed.
    bool load(const std::string& path);
    using LoadMeshFn = std::function<eng::MeshHandle(const std::string& path)>;
    void resolveMeshes(const LoadMeshFn& loadFn);
    // Turn authored Triggers into sensor colliders, then reconcile the world.
    void buildAll();
    glm::vec3 playerSpawn() const;
    glm::vec3 levelExit() const;
    float exitYawDegrees() const;
    std::vector<ScenePlacement> placements(const std::string& prefix = {}) const;

    // Authored EnemySpawn components, reported in the same shape as a marker so
    // one encounter path consumes both.
    //
    // The editor's catalogue offers "enemy spawn", which writes this component;
    // the spawner only ever read markers named "enemy.<id>". So the button
    // placed something the game silently ignored -- the entity was in the file,
    // in the outliner and in the cooked map, and no enemy ever appeared.
    // `type` comes back already prefixed ("enemy.goblin") so the two sources
    // are indistinguishable downstream.
    std::vector<ScenePlacement> enemySpawnPlacements() const;

    // Authored Pickup components, likewise: type is "pickup.<id>".
    std::vector<ScenePlacement> pickupPlacements() const;

    // The palette the level asks to be lit and graded with, or empty for the
    // game's default.
    std::string palette() const;

    entt::registry& registry() { return mWorld.registry(); }
    const entt::registry& registry() const { return mWorld.registry(); }

private:
    eng::ecs::World& mWorld;
    uint32_t mGroup = 0;
};

} // namespace game
