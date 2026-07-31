#pragma once
#include <eng/DebugTools.h>
#include <eng/debug/Console.h>
#include <eng/FrameStats.h>
#include <eng/Physics.h>
#include <eng/app/Application.h>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace eng {

class FpsController;

// Everything an FPS needs decided before the first frame, beyond what
// AppConfig already covers.
struct FpsGameConfig
{
    // Collision layers and their matrix. Deliberately not defaulted to a
    // taxonomy: which layers exist is the game's model, not the engine's.
    PhysicsSetup physics = PhysicsSetup::generic();
    // Which of those layers are "the static level", for the collider overlay's
    // hide-the-walls toggle.
    CollisionMask staticLayers = kNoLayers;

    // First-person camera. These are the numbers every level of this game runs
    // with, so they belong in one place instead of at the top of each onStart.
    float cameraFov = 70.0f;
    float nearClip = 0.08f;
    float farClip = 90.0f;

    // Frame-timing phases, in order: what the game measures. The HUD plots
    // them; the engine never learns what they mean. Phase 0 is the fixed-step
    // simulation, timed by the base. A trailing "Render" phase is appended
    // automatically -- the app never sees the render call, so it could not time
    // it, and a breakdown missing the most expensive phase is worse than none.
    std::vector<std::string> phases;

    // Input action names (as bound in the TOML config). Empty disables that
    // binding, for a game that does not want it.
    std::string quitAction = "quit";
    std::string consoleAction = "debug_ui";       // F1
    std::string colliderAction = "show_colliders"; // F3
    std::string perfAction = "show_perf";          // F4
    std::string devConsoleAction = "dev_console";  // backquote

    // Compile materials and upload textures at the end of the load plan, while
    // the loading screen is still up. Off only for a mode that builds no world
    // and would just be paying for it (the material staging scene).
    bool warmupRenderer = true;
};

// The genre base class: a first-person game with physics, a debug console, a
// collider overlay and a frame-time HUD, wired in the order that works.
//
// This is the whole point of an engine that is not trying to be generic. The
// frame ordering below is not a suggestion -- it is the arrangement this game
// needs, and every level, sample and test mode gets it by inheriting rather
// than by re-deriving it in its own main():
//
//   onFrameBegin  mouse capture policy, debug toggles, then onInput()
//   onFixedStep   onPreSimulate() -> Physics::update() -> onPostSimulate()
//   onUpdate      Physics::setInterpolationAlpha(alpha), then onPresent()
//   onGui         console + perf HUD + collider overlay, then onGameGui()
//
// Look runs at render rate in onInput, locomotion at the fixed rate in
// onPreSimulate: splitting them is what keeps the view responsive without
// making movement depend on frame rate. That split is doctrine here, not a
// per-game choice.
//
// A mode with no fixed step (AppConfig::fixedDt <= 0) never gets
// onPreSimulate/onPostSimulate and must step physics itself from onPresent;
// everything else still applies.
class FpsGameApp : public Application
{
public:
    // Both out of line: Impl is incomplete here, and the implicit versions would
    // need its size.
    FpsGameApp();
    ~FpsGameApp() override;

    // Application seams, sealed: subclasses override the hooks below instead,
    // so the frame order stays the engine's.
    void onLoad(Engine& engine, LoadPlan& plan) final;
    bool onStart(Engine& engine) final;
    void onFrameBegin(const FrameContext& f) final;
    void onFixedStep(const FrameContext& f, float fixedDt) final;
    void onUpdate(const FrameContext& f) final;
    void onGui(const FrameContext& f) final;
    void onFrameRendered(float renderMs) final;
    void onShutdown(Engine& engine) final;

protected:
    // --- required -------------------------------------------------------
    // Runs before physics comes up and before any scene build.
    virtual FpsGameConfig setup(Engine& engine) = 0;
    // Build the world. Physics, console and overlays already exist. Runs as
    // one step of the load plan, named "Building the world".
    //
    // A game whose build is slow enough to want a progress bar of its own does
    // not put it all here: it overrides onLoadGame and adds named steps, which
    // run *before* this. Then onStartGame is whatever is left over.
    virtual bool onStartGame(Engine& engine) = 0;

    // Game-owned load steps. Physics and the console exist; the world does
    // not yet. Steps added here run before onStartGame and after the engine's
    // own setup, with the loading screen up.
    virtual void onLoadGame(Engine& engine, LoadPlan& plan)
    {
        (void)engine;
        (void)plan;
    }

    // --- per-frame hooks ------------------------------------------------
    // Render-rate input: camera look, weapon switches, mode toggles.
    virtual void onInput(const FrameContext& f) {}
    // Fixed-rate, before Physics::update: post character velocities here.
    virtual void onPreSimulate(const FrameContext& f, float fixedDt) {}
    // Fixed-rate, after Physics::update: contacts have resolved.
    virtual void onPostSimulate(const FrameContext& f, float fixedDt) {}
    // Once per rendered frame: copy simulation state onto render nodes,
    // interpolating with f.alpha.
    virtual void onPresent(const FrameContext& f) {}
    // Application imgui, drawn after the engine's console/HUD.
    virtual void onGameGui(const FrameContext& f) {}
    // Teardown while the renderer is still alive; physics shuts down after.
    virtual void onStopGame(Engine& engine) {}

    // --- what the base needs from the game ------------------------------
    // Where the player's eye is: the collider overlay fades by distance from
    // it. Defaults to the controller's eye when playerController() is given.
    virtual glm::vec3 viewerPosition() const;
    // Lets the console's Player tab tune the real controller. Null is fine.
    virtual FpsController* playerController() { return nullptr; }

    // --- services -------------------------------------------------------
    Physics& physics();
    DebugTools& console();     // add game tabs with console().addPanel(...)
    // Log + command line. Register game commands with
    // devConsole().registerCommand(...) from onStartGame.
    DebugConsole& devConsole();
    FrameStats& stats();
    ColliderDebug& colliderView();
    const FpsGameConfig& config() const;

    // True while either debug surface is open: the sim is meant to be frozen
    // and the mouse belongs to the UI, not the camera.
    bool uiOpen() const;
    // Whether the game should drive the player this frame. Games with their own
    // extra freeze conditions (a preview mode, a cutscene) override this.
    virtual bool playerDriven() const { return !uiOpen(); }

private:
    // First step of the load plan: stats, camera, physics, console commands.
    void startSystems(Engine& engine);

    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
