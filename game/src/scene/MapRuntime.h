#pragma once

#include <eng/ecs/World.h>
#include <eng/ecs/components/FirstPersonController.h>
#include <eng/ecs/components/ScreenCamera.h>
#include <eng/ecs/components/ThirdPersonCamera.h>
#include <eng/runtime/SceneRuntime.h>

#include "ViewmodelRig.h"
#include "audio/ActorSounds.h"

#include <entt/entt.hpp>
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

// This game's half of reading a cooked .map: the authored queries that are
// about *this game's* vocabulary -- where the player spawns, where the exit is,
// what enemies and pickups were placed, what the viewmodel was tuned to.
//
// Everything generic underneath -- decoding the file, merging it into the
// world, resolving meshes and primitives, reading the authored camera rig --
// is eng::runtime::SceneRuntime, which this holds. The split is by vocabulary,
// not by convenience: what is in here names game::Exit, game::EnemySpawn and
// ViewmodelRig, and what is down there does not, which is exactly what lets
// raven_player play a scene without linking any of this.
//
// It does not own a world. A level has exactly one (see eng::ecs::World);
// load() merges into the world it is given.
class MapRuntime {
public:
    // `group` is stamped on every entity this map contributes, so a level
    // transition destroys exactly the map's entities and leaves the player and
    // anything else persistent alone (eng::ecs::World::destroyGroup).
    MapRuntime(eng::ecs::World& world, uint32_t group);

    // Merges the cooked map's entities into the world. Returns false and adds
    // nothing if the file is missing or malformed.
    bool load(const std::string& path);
    using LoadMeshFn = eng::runtime::SceneRuntime::LoadMeshFn;
    void resolveMeshes(const LoadMeshFn& loadFn);
    // The generated half of the same job: entities carrying a PrimitiveMesh get
    // geometry built for them rather than loaded.
    void resolvePrimitives(eng::Renderer& renderer);
    // Turn authored Triggers into sensor colliders, then reconcile the world.
    void buildAll();

    // The authored PlayerSpawn, which is this game's marker rather than an
    // engine concept -- hence here and not in SceneRuntime.
    glm::vec3 playerSpawn() const;
    // Which way the spawn faces, in radians, or 0 when it states none.
    //
    // Read from the marker's own rotation, because that is where an author puts
    // it -- and until this existed the rotation was silently dropped, so a
    // village whose street ran north spawned the player looking at the wall
    // behind them. The engine convention is that forward is -Z, so a marker
    // with no rotation faces -Z and the yaw is measured from there.
    float playerSpawnYaw() const;
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
    //
    // Three of the four come straight from the engine's rig; `viewmodel` is
    // this game's and is read here.
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

    entt::registry& registry() { return mScene.registry(); }
    const entt::registry& registry() const { return mScene.registry(); }

    // The generic runtime underneath, for a caller that wants what it reports
    // rather than this game's reading of it.
    eng::runtime::SceneRuntime& scene() { return mScene; }

private:
    eng::runtime::SceneRuntime mScene;
};

} // namespace game
