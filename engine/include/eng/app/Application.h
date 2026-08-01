#pragma once
#include <eng/Engine.h>
#include <eng/Loading.h>

#include <chrono>
#include <string>

namespace eng {

// What the runner needs to know before it can bring the Engine up. Returned by
// Application::configure(), which is the only callback that runs before there
// is a window: nothing in it may touch the renderer.
struct AppConfig
{
    // A LOGICAL path, resolved against the mounted packs -- "config/game.toml", not a
    // directory plus a filename. The app no longer knows where its content
    // lives on disk; assets.toml does.
    std::string configPath;
    // Which set from assets.toml's [mounts] this app runs on: "game",
    // "editor", "demo". It picks the packs and their priority order, and with
    // them every resource location the renderer registers.
    std::string mountSet = "game";
    // Starting render profile (eng::renderPresetFromArgs / renderPresetFromName).
    // 0 leaves the choice to PSX_RENDER_PRESET and then kDefaultRenderPreset.
    // Only the *starting* look: the debug console switches profiles at runtime.
    int renderPreset = 0;

    // Fixed-step simulation. fixedDt <= 0 disables the fixed loop entirely
    // (onFixedStep is never called and alpha stays 1), which is what a purely
    // presentational app -- the demo -- wants.
    float fixedDt = 1.0f / 60.0f;
    // Spiral-of-death guard: never run more than this many fixed steps for one
    // rendered frame, even if the frame took a second.
    int maxFixedSteps = 5;

    // Pair beginImGuiFrame/renderFrame every frame and call onGui between them.
    // Off by default because a frame that opens an imgui frame must also close
    // it, and an app with no debug UI should not pay for that.
    bool imgui = false;

    // --- startup loading -------------------------------------------------
    // Draw the animated loading screen while the app's LoadPlan runs. Off means
    // the plan still runs (the window just stays blank), which is what a
    // headless capture or a tool wants.
    bool loadingScreen = true;
    // Milliseconds of loading work per frame. Small enough that the ring keeps
    // spinning; a single step heavier than this still runs to completion.
    float loadBudgetMs = 8.0f;
    std::string loadingTitle; // empty falls back to window.title
    std::string loadingHint;  // optional flavour line
};

// Per-frame state handed to every frame callback. Constructed by the runner;
// apps only read it.
struct FrameContext
{
    Engine& engine;
    // Game-time delta: the spike-clamped wall delta after the game clock's
    // scale and pause (eng::Clock). Zero on a paused frame, which is what makes
    // pause and slow-motion work without any system opting in. Simulation and
    // presentation both read this.
    float dt;
    float alpha; // fixed-step interpolation alpha in [0,1); 1 with no fixed loop
    uint64_t frame; // frames rendered so far (0 on the first one)
    // Unscaled wall delta. For the few things that must keep moving while the
    // world is frozen: debug camera, UI animation, profiling readouts.
    float realDt = 0.0f;
};

// The entrypoint contract: an app is a set of ordered callbacks, and
// runApplication() owns the loop that drives them. Exists so main() stops being
// a hand-rolled loop per executable -- the ordering (input -> fixed sim ->
// present -> gui -> render) is engine policy, not something each game re-derives
// and gets subtly wrong.
//
// Call order per frame:
//   Engine::tick()  ->  onFrameBegin  ->  onFixedStep xN  ->  onUpdate
//                   ->  [beginImGuiFrame -> onGui]        ->  renderFrame
class Application
{
public:
    virtual ~Application() = default;

    // Runs before Engine::init. No renderer, no window, no input yet.
    virtual AppConfig configure(int argc, char** argv) = 0;

    // Startup work, as named steps. The runner pumps the plan a few
    // milliseconds per frame with the loading screen up, so anything slow --
    // level cook, asset libraries, physics build, shader warmup -- belongs
    // here rather than in onStart, where it would freeze a blank window.
    // Steps run in order, on the main thread, before onStart.
    virtual void onLoad(Engine& engine, LoadPlan& plan)
    {
        (void)engine;
        (void)plan;
    }

    // Scene build. Return false to abort the run with exitCode. Runs after the
    // load plan has finished; keep it to whatever must happen last.
    virtual bool onStart(Engine& engine) = 0;

    // Input and mode toggles, once per rendered frame at full rate.
    virtual void onFrameBegin(const FrameContext& f) { (void)f; }
    // Simulation. Called 0..maxFixedSteps times per frame with an exactly fixed
    // delta; anything determinism-sensitive (physics, locomotion) belongs here.
    virtual void onFixedStep(const FrameContext& f, float fixedDt)
    {
        (void)f;
        (void)fixedDt;
    }
    // Presentation: copy simulation state onto render nodes, animate, interpolate
    // with f.alpha. Runs once per rendered frame.
    virtual void onUpdate(const FrameContext& f) { (void)f; }
    // Debug UI. Only called when AppConfig::imgui is true.
    virtual void onGui(const FrameContext& f) { (void)f; }
    // How long this frame's render took, in milliseconds. Called after the
    // frame is submitted, so an app that plots phase timings can show the cost
    // of the one phase it does not own.
    virtual void onFrameRendered(float renderMs) { (void)renderMs; }

    // Teardown that must happen while the renderer is still alive (physics
    // bodies, scene nodes). Engine::shutdown() runs after this returns.
    virtual void onShutdown(Engine& engine) { (void)engine; }

    // Process exit code; an app may set it from any callback.
    int exitCode = 0;
};

// Owns the Engine, runs the loop, returns app.exitCode (or 1 if init/onStart
// failed). This is what main() should contain.
int runApplication(Application& app, int argc = 0, char** argv = nullptr);

} // namespace eng
