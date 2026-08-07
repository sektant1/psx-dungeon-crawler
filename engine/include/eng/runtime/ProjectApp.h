#pragma once

#include <eng/app/FpsGameApp.h>
#include <eng/camera/ScreenCameraRig.h>
#include <eng/controllers/FpsController.h>
#include <eng/ecs/RendererSceneBackend.h>
#include <eng/particles/ParticleLibrary.h>
#include <eng/render/GifRecorder.h>
#include <eng/runtime/Project.h>
#include <eng/runtime/ProjectComponents.h>
#include <eng/runtime/SceneRuntime.h>
#include <eng/script/ScriptHost.h>

#include <optional>
#include <string>
#include <vector>

namespace eng::runtime {

// Plays a project: mount its content, load its scene, run the script host over
// it, and walk around.
//
// This is the generic half of what game::MapPlayApp used to be alone -- the
// arrangement of world, physics, renderer backend, script host and controller
// that any game on this engine needs, with nothing in it that knows what THIS
// game is. raven_player instantiates it as-is; the dungeon crawler derives
// from it and adds its own vocabulary through the hooks below, which is what
// keeps one scene-boot path in the tree instead of two that drift.
//
// The frame order is inherited from FpsGameApp and is not a per-project
// choice. What a subclass gets to decide is listed under "seams" below, and
// each one exists because this game needed it -- not speculatively.
class ProjectApp : public FpsGameApp
{
public:
    explicit ProjectApp(Project project);

    // What this app plays. A subclass that boots a bare .map instead of a
    // project (the game's `game <file.map>` path) constructs with a Project
    // whose dir is empty and whose mainScene is that file.
    const Project& project() const { return mProject; }

    // --record. Started after the scene is fully built, so the clip's first
    // frame is a finished world: recording pins the frame delta, and a load
    // hitch would otherwise be baked into the timing of the whole clip.
    void setRecording(std::optional<RecordingOptions> options);

    AppConfig configure(int argc, char** argv) override;

protected:
    // --- seams ----------------------------------------------------------
    // The component vocabulary the scene is read with. The engine's own by
    // default, which is what makes a project playable without the game.
    virtual const ecs::ComponentRegistry& components() const;

    // Collision layers, HUD phases, camera clipping. The default is a generic
    // physics setup with no layer taxonomy -- deliberately, since which layers
    // exist is an application's model, not the engine's.
    FpsGameConfig setup(Engine& engine) override;

    // After the world is attached to renderer, physics and audio, and before
    // the scene is read. Where a subclass installs things that must exist
    // before entities do -- this game's particle collider.
    virtual void onWorldAttached(Engine& engine) {}

    // Runs against the registry with the scene loaded and resolved but not yet
    // synced: the seam for turning an application's authored components into
    // engine ones. This game materialises its Triggers into sensor colliders
    // here.
    virtual void onBeforeSync(entt::registry& registry) {}

    // After the scene is built and lit, before the player is placed. Where a
    // subclass adds presentation the scene implies rather than states -- this
    // game builds a portal at every authored Exit.
    virtual void onSceneBuilt(Engine& engine, SceneRuntime& scene) {}

    // Where to stand the player. Defaults to the scene's authored rig; this
    // game overrides it to honour its own PlayerSpawn marker.
    virtual glm::vec3 playerStart(const SceneRuntime& scene) const;

    // Before the world detaches and the renderer goes.
    virtual void onWorldDetaching(Engine& engine) {}

    // --- what subclasses may reach --------------------------------------
    ecs::World& world() { return mWorld; }
    SceneRuntime& scene() { return *mScene; }
    FpsController& player() { return mPlayer; }

    // --- FpsGameApp -----------------------------------------------------
    void onLoadGame(Engine& engine, LoadPlan& plan) final;
    bool onStartGame(Engine& engine) final;
    void onPresent(const FrameContext& f) final;
    void onStopGame(Engine& engine) final;
    FpsController* playerController() final { return &mPlayer; }

private:
    // The cooked map this app opens: the project's, unless RAVEN_PLAY_MAP
    // names one. Reported as an absolute path, empty when it cannot be found.
    std::string scenePath() const;
    // The cooked form of an authored path ("scenes/level2.scn"), as
    // game.load_scene names it.
    std::string cookedPathFor(const std::string& scene) const;

    // Everything a scene needs after it is read: meshes, primitives, the build
    // hook, the default light. Shared by the first load and every switch, so a
    // scene reached through a door is built exactly like the one the project
    // started on.
    bool buildScene(Engine& engine, const std::string& path);
    // How a MeshSource path becomes a mesh. One definition, used by the load
    // pass and by the per-frame pass that catches what a script spawned --
    // those must agree, or a spawned scene looks different from a placed one.
    MeshHandle loadSceneMesh(Renderer& renderer, const std::string& path) const;
    // Constructs the script host over the current world and binds everything
    // it is allowed to reach. Called after every scene build, because a host
    // binds to a World for its whole life and a scene switch replaces the
    // contents of that world under it.
    void startScripts(Engine& engine);
    // Acts on a pending game.load_scene. Runs at the top of a frame, never from
    // inside script dispatch: a script asking for a new level is running on an
    // instance that the switch is about to destroy.
    void applyPendingScene(Engine& engine);

    // Decides what the camera is: an authored one makes the scene a shot, a
    // ScreenCamera makes it a page, neither means the player controller drives.
    // Called after every scene build, because a switch can change the answer.
    void adoptSceneCamera(Engine& engine);

    // Lifetime groups, handed out monotonically and never reused.
    //
    // One allocator for both the scene and everything spawned into it, because
    // the two must never collide and only this class sees both. It lives here
    // rather than in SceneRuntime because SceneRuntime is REPLACED on every
    // scene change -- a counter in there restarts, and would hand a new spawn
    // the group a survivor of the previous scene was already using.
    //
    // Starts at 1: group 0 means "ungrouped" and World::destroyGroup rejects
    // it, since destroying it would take the player with it.
    uint32_t nextGroup() { return ++mLastGroup; }
    uint32_t mLastGroup = 0;
    uint32_t mSceneGroup = 0;
    // Everything game.spawn_scene has produced and not despawned. Destroyed
    // with the scene that spawned it: before this they outlived it, in groups
    // nothing held a handle to any more, which is a leak no script could clear.
    std::vector<uint32_t> mSpawnedGroups;
    std::string mPendingScene; // non-empty between the request and the switch

    Project mProject;
    // What the project declared for itself, and the table the engine's
    // components plus those make. Both must outlive every scene read with them,
    // so they sit here rather than being rebuilt per scene: the registry holds
    // raw pointers into the schema's names and field tables.
    ProjectComponents mDeclared;
    ecs::ComponentRegistry mComponents;
    bool mComponentsBuilt = false;
    ecs::World mWorld;
    std::optional<ecs::RendererSceneBackend> mBackend;
    std::optional<SceneRuntime> mScene;
    // Constructed in onStartGame, once the world and physics exist. A
    // ScriptHost binds to a World for its whole life, which is why this is an
    // optional rather than a plain member.
    std::optional<script::ScriptHost> mScripts;
    // Present only for a screen scene; its presence is what "this scene is a
    // screen" means at runtime.
    std::optional<ScreenCameraRig> mScreen;
    ParticleLibrary mParticles;
    bool mParticlesReady = false;
    FpsController mPlayer;
    std::optional<RecordingOptions> mRecording;
    // The scene owns the camera: decided once at load, because a camera
    // appearing mid-play would take the view away from a player already
    // walking around.
    bool mCinematic = false;
};

} // namespace eng::runtime
