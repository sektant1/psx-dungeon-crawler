#pragma once

#include <eng/app/FpsGameApp.h>
#include <eng/camera/ScreenCameraRig.h>
#include <eng/controllers/FpsController.h>
#include <eng/ecs/RendererSceneBackend.h>
#include <eng/particles/ParticleLibrary.h>
#include <eng/render/GifRecorder.h>
#include <eng/runtime/Project.h>
#include <eng/runtime/SceneRuntime.h>
#include <eng/script/ScriptHost.h>

#include <optional>
#include <string>

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

    Project mProject;
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
