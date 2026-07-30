#pragma once
#include <eng/Engine.h>

#include <chrono>
#include <string>

namespace eng {

// What the runner needs to know before it can bring the Engine up. Returned by
// Application::configure(), which is the only callback that runs before there
// is a window: nothing in it may touch the renderer.
struct AppConfig
{
    std::string configPath; // TOML passed to Engine::init
    std::string assetDir;   // app asset root
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
};

// Per-frame state handed to every frame callback. Constructed by the runner;
// apps only read it.
struct FrameContext
{
    Engine& engine;
    float dt;    // wall-clock delta for this frame, already spike-clamped
    float alpha; // fixed-step interpolation alpha in [0,1); 1 with no fixed loop
    uint64_t frame; // frames rendered so far (0 on the first one)
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

    // Scene build. Return false to abort the run with exitCode.
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
