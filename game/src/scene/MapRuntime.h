#pragma once

#include <eng/ecs/MeshResolve.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/FirstPersonController.h>
#include <eng/ecs/components/ScreenCamera.h>
#include <eng/ecs/components/ThirdPersonCamera.h>

#include "ViewmodelRig.h"
#include "audio/ActorSounds.h"

#include <entt/entt.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
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
    // The generated half of the same job: entities carrying a PrimitiveMesh get
    // geometry built for them rather than loaded.
    //
    // A renderer rather than a callback, because unlike a mesh path there is
    // nothing for a caller to disagree about -- the description IS the geometry,
    // and the engine's own resolver builds it. The cache lives here so a level
    // transition releases exactly the meshes that level generated.
    void resolvePrimitives(eng::Renderer& renderer);
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

    // Authored Npc components; type is "npc.<id>". Same prefixing rule as the
    // two above, so a hand-written "npc.ilsabet" marker and the editor's Npc
    // component are indistinguishable by the time anything acts on them.
    std::vector<ScenePlacement> npcPlacements() const;

    // The palette the level asks to be lit and graded with, or empty for the
    // game's default.
    std::string palette() const;

    // The player rig a level authored, if it did: how the body moves and where
    // the first-person hands sit, both authored on the camera that is the
    // player's eye (see eng::ecs::FirstPersonController, game::ViewmodelRig).
    //
    // Optional per half, because they are separate components an author adds
    // separately: a level that only wants a wider FOV should not have to
    // restate the whole viewmodel framing to get one. Absent means the game's
    // own config, which is every level authored before these existed.
    struct AuthoredPlayerRig {
        std::optional<eng::ecs::FirstPersonController> controller;
        std::optional<ViewmodelRig> viewmodel;
        // The camera shape this level asks for. Present means "play me over
        // the shoulder"; absent means the game's own default, which is first
        // person. A level that authors both gets third person -- the shape is
        // the more specific statement, and the first-person block still
        // supplies the movement numbers both shapes share.
        std::optional<eng::ecs::ThirdPersonCamera> thirdPerson;
        // Present means this scene is not a world at all but a 2D screen: a
        // menu, a HUD plate, a dialogue page.
        std::optional<eng::ecs::ScreenCamera> screen;
    };
    AuthoredPlayerRig playerRig() const;

    entt::registry& registry() { return mWorld.registry(); }
    const entt::registry& registry() const { return mWorld.registry(); }

private:
    eng::ecs::World& mWorld;
    uint32_t mGroup = 0;
    // Generated meshes this map's entities share. Two hundred greybox blocks
    // with four distinct sizes between them are four vertex buffers, not two
    // hundred.
    eng::ecs::PrimitiveMeshCache mPrimitives;
};

} // namespace game
