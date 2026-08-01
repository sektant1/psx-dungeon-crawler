#pragma once
#include <eng/Handles.h>
#include <eng/ecs/World.h>

#include <entt/entt.hpp>

#include <vector>

namespace eng::ecs {

class SceneBackend;

// Drives the backend (renderer) from the registry each frame: allocates a
// backing node when an entity first needs one, pushes changed transforms, and
// frees nodes when entities are destroyed. Renderer is a view; the registry is
// the source of truth.
//
// An entity gets a node when it asks for one -- MeshRenderer, LightRef, or a
// bare RenderNode tag for an attachment parent that draws nothing itself. Not
// merely for having a Transform: once one World holds the whole game, most
// entities (a combatant's stats, a spawner, a trigger volume) have a position
// and nothing to draw, and giving each of those a renderer node costs a node
// and a transform push per frame for nothing.
//
// Owned by World (World::attachRenderer); construct one directly only in a test.
class SceneSync : public WorldReconciler {
public:
    SceneSync(World& world, SceneBackend& backend);
    ~SceneSync() override;

    SceneSync(const SceneSync&) = delete;
    SceneSync& operator=(const SceneSync&) = delete;

    // Whether an authored Camera in this world may take the renderer's camera.
    //
    // True for a world that IS the game's view. False for one that is merely
    // being looked at -- the editor previews a document whose cameras are
    // content, and a preview that grabbed the viewport would yank it away from
    // the author on every rebuild. The distinction is per world, not per scene,
    // because it is a fact about who is watching rather than about the file.
    void setDrivesCamera(bool drives) { mDrivesCamera = drives; }

    // Run once per frame, after gameplay mutated the registry: reconciles the
    // backend with the registry (also calls World::updateWorldTransforms).
    void sync() override;
    // Destroy all materialised backend nodes. Used before replacing a whole
    // scene document and called automatically on destruction.
    void clear() override;

private:
    struct Tracked {
        entt::entity entity{entt::null};
        NodeHandle node;
        // What was last pushed, so a state (rather than an event) is only sent
        // when it changes. Lives here and not in a registry component because
        // it is this reconciler's memory of its own backend, exactly like
        // mTracked itself -- nothing else may read it, and it must not be saved.
        bool visible = true;
    };

    // The camera currently being looked through, and the lens last pushed for
    // it. Remembered so attaching happens when the shot changes rather than
    // every frame, and so a scene with no Camera at all never touches the
    // renderer's camera -- which is what lets a game keep driving its own.
    void syncCamera();

    World& mWorld;
    SceneBackend& mBackend;
    std::vector<Tracked> mTracked;
    std::vector<entt::entity> mPushThisFrame;
    entt::entity mCameraEntity{entt::null};
    Camera mCameraLens{};
    bool mCameraPushed = false;
    bool mDrivesCamera = true;
};

} // namespace eng::ecs
