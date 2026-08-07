#pragma once

#include <eng/ecs/MeshResolve.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/FirstPersonController.h>
#include <eng/ecs/components/ScreenCamera.h>
#include <eng/ecs/components/ThirdPersonCamera.h>

#include <entt/entt.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <string>

namespace eng {
class Renderer;
}

namespace eng::runtime {

// Reads a cooked .map into a world and answers the questions any runtime asks
// of one: where the meshes are, what the author asked the camera to be, where
// the player starts.
//
// This is the generic half of what game::MapRuntime used to do alone. The
// split is by *vocabulary*: everything here touches engine components only, so
// it works for a scene made in a project that has never heard of this game.
// Everything that reads a game::Exit, a game::EnemySpawn or a ViewmodelRig
// stayed behind in game::MapRuntime, which now holds one of these.
//
// It does not own the world. A level has exactly one (see eng::ecs::World);
// load() merges into the world it is given, tagging what it adds with `group`
// so a scene change can destroy exactly this scene's entities and leave the
// player standing.
//
// The component table is a constructor parameter rather than a global, which
// is the whole reason a project can play a scene: the game passes its own
// table (engine components plus its markers), the player passes the engine's,
// and neither has to know the other exists.
class SceneRuntime {
public:
    SceneRuntime(ecs::World& world, uint32_t group,
                 const ecs::ComponentRegistry& components);

    // Merges the cooked map's entities into the world. Returns false and adds
    // nothing if the file is missing or malformed. Components carrying a
    // stable id this runtime's table does not know are skipped, not refused --
    // which is what lets the player open a scene authored against a richer
    // vocabulary and get everything it does understand.
    bool load(const std::string& path);

    using LoadMeshFn = std::function<MeshHandle(const std::string& path)>;
    void resolveMeshes(const LoadMeshFn& loadFn);

    // The generated half of the same job: entities carrying a PrimitiveMesh
    // get geometry built rather than loaded. The cache lives here so a scene
    // change releases exactly the meshes that scene generated.
    void resolvePrimitives(Renderer& renderer);

    // The same, for entities that have appeared since -- what a script spawns
    // mid-level. Cheap enough to call every frame: it skips anything already
    // holding geometry, so a level that spawns nothing pays one view walk.
    void resolveNewPrimitives(Renderer& renderer);

    // Merges a cooked scene into the world at `origin`, as a group of its own,
    // and returns that group -- or 0 if it could not be read.
    //
    // The runtime half of scene instancing. Cook-time instancing (an `instance`
    // entity in a .scn) covers what a level is built out of; this covers what a
    // level produces while it runs: an enemy, a projectile, a dropped item. The
    // two are deliberately the same asset -- a torch.scn placed by an author and
    // one spawned by a script are the same file -- because the alternative is
    // authoring every spawnable thing twice.
    //
    // The returned group is what despawns it again (World::destroyGroup), and
    // is distinct from the scene's own group, so destroying the level does not
    // take the player's spawned effects with it and vice versa.
    //
    // Entities arrive with their meshes unresolved; the runtime's per-frame
    // resolveNewPrimitives and mesh pass pick them up on the next frame, the
    // same way anything a script spawns does.
    uint32_t instantiate(const std::string& path, const glm::vec3& origin);

    // Last step of the build. `before` runs against the registry with
    // everything loaded and resolved but nothing synced yet -- the seam for
    // turning an application's own authored components into engine ones (this
    // game materialises its Triggers into sensor colliders there). Empty is
    // the normal case.
    using BuildHook = std::function<void(entt::registry&)>;
    void buildAll(const BuildHook& before = {});

    // Where to stand the player when the scene does not say otherwise.
    //
    // The authored FirstPersonController is the marker, because that component
    // *is* the statement "the player is here, and moves like this" -- an engine
    // concept, unlike this game's PlayerSpawn. A scene with none starts the
    // player just above the origin, which is where a floor built at y=0 wants
    // them.
    glm::vec3 playerSpawn() const;

    // What the scene asks the camera to be. Each half is independently
    // optional: a scene that only wants a wider FOV should not have to restate
    // the rest to get it.
    struct AuthoredRig {
        std::optional<ecs::FirstPersonController> controller;
        // Present means "play me over the shoulder".
        std::optional<ecs::ThirdPersonCamera> thirdPerson;
        // Present means this scene is not a world at all but a 2D screen: a
        // menu, a HUD plate, a dialogue page.
        std::optional<ecs::ScreenCamera> screen;
    };
    AuthoredRig rig() const;

    // Did the author place a camera? A scene that did is a shot -- it plays
    // itself, and a player controller would fight it for the renderer's one
    // camera every frame.
    bool hasAuthoredCamera() const;

    entt::registry& registry() { return mWorld.registry(); }
    const entt::registry& registry() const { return mWorld.registry(); }

private:
    ecs::World& mWorld;
    const ecs::ComponentRegistry& mComponents;
    uint32_t mGroup = 0;
    // Groups handed out by instantiate(). Starts above the scene's own so a
    // spawned object can never share a group with the level that spawned it.
    uint32_t mLastSpawnGroup = 0;
    ecs::PrimitiveMeshCache mPrimitives;
};

// Does this cooked map author a camera?
//
// Asked before a world exists, because for some callers it is what decides
// which loop runs at all. Reads the file into a scratch registry -- cheap next
// to building a scene. False for a map that is missing or malformed, so a
// broken file still reaches the loop that reports it rather than being
// diagnosed here.
bool mapHasCamera(const std::string& path,
                  const ecs::ComponentRegistry& components);

} // namespace eng::runtime
