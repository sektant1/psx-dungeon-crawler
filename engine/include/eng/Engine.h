#pragma once
#include <eng/Config.h>
#include <eng/Input.h>
#include <eng/RenderPresetInfo.h>
#include <eng/Renderer.h>
#include <eng/StepClock.h>
#include <eng/render/GifRecorder.h>
#include <eng/systems/System.h>

#include <memory>
#include <string>
#include <vector>

namespace eng {

// Owns lifetime and ordering: SDL window -> Ogre Root -> (frames) ->
// Ogre Root down -> SDL window down. Also owns the frame clock and the
// PSX_SCREENSHOT verification hook (render 90 frames by default, save PNG,
// close). PSX_SCREENSHOT_FRAME overrides the capture frame for animation tests.
class Engine
{
public:
    Engine();
    ~Engine();

    // Loads TOML config (window.title/width/height + [bindings]), creates
    // the window, brings up the renderer with engine + app asset roots.
    // `renderPreset` is the starting render profile (0 = let PSX_RENDER_PRESET
    // decide, then the engine default). The applied id is readable afterwards
    // so the debug console can open on the look that is actually live instead
    // of assuming the default.
    bool init(const std::string& configPath, const std::string& appAssetDir,
              int renderPreset = 0);

    // The render profile currently applied. Set by init and by
    // setRenderPreset; the console's own switcher reports through the latter.
    int renderPreset() const { return mRenderPreset; }
    void setRenderPreset(int id);

    float tick(); // pump events, update input; returns dt clamped to 0.1 s
    bool shouldClose() const { return mClose; }
    void requestClose() { mClose = true; }
    // dt = wall time (drives the benchmark/screenshot hooks); animDt = the time
    // Ogre advances particles + time-based animation by.
    //
    // Pass animDt < 0 (the default) to take it from the step clock's Particles
    // channel, so particle VFX are stepped in step with everything else without
    // each game having to remember to do it. Pass an explicit value to override,
    // or 0 to freeze Ogre-side animation for a frame.
    void renderFrame(float dt, float animDt = -1.0f);
    void shutdown();

    // Record a fixed-length clip and encode it to a GIF, then close. Call
    // before the first renderFrame (onStart is the natural place). Recording
    // pins the frame delta to 1/fps, so the clip plays back at the speed it was
    // simulated at and is reproducible run to run -- the same guarantee the
    // screenshot hook relies on.
    void startRecording(const RecordingOptions& options);
    bool recording() const;

    // --- Dear ImGui debug overlay ----------------------------------------
    // Call beginImGuiFrame(dt) once per frame BEFORE building any ImGui/ImGuizmo
    // windows, then build them; renderFrame(dt) paints them over the window.
    // Frames where beginImGuiFrame is not called draw nothing. imguiWants*()
    // report whether imgui is currently eating input, so the game can gate
    // gameplay controls while the console is open.
    void beginImGuiFrame(float dt);
    bool imguiReady() const;
    bool imguiWantsMouse() const;
    bool imguiWantsKeyboard() const;

    Renderer& renderer() { return mRenderer; }
    Input& input() { return mInput; }
    Config& config() { return mConfig; }

    // Quantised animation clock (stop-motion / OSRS stepping). tick() advances
    // it, so a game only reads from it: ask each animated system's channel for
    // its time/delta instead of using the raw frame delta. Rates are seeded from
    // the [animation] config table in init() and are live-tweakable.
    StepClock& stepClock() { return mStepClock; }
    const StepClock& stepClock() const { return mStepClock; }

    // System registry (SPEngine-style). Additive: the existing tick()/
    // renderFrame() loop is unchanged; a game opts in by registering systems
    // and calling updateSystems(dt) from its loop.
    System* registerSystem(System::StrongPtr sys);
    void updateSystems(float dt);

    template <typename T> T* getSystem() {
        for (auto& s : mSystems)
            if (auto* p = dynamic_cast<T*>(s.get())) return p;
        return nullptr;
    }

private:
    // Frame-clock half of tick(): frame limiter, fixed-step capture override,
    // and the 0.1 s spike clamp. Split out so tick() has one exit point and can
    // always feed the step clock.
    float measureFrameDelta();

    struct Impl;
    std::unique_ptr<Impl> mImpl;
    Config mConfig;
    Input mInput;
    Renderer mRenderer;
    StepClock mStepClock;
    int mRenderPreset = kDefaultRenderPreset;
    bool mClose = false;
    std::vector<System::StrongPtr> mSystems;
};

} // namespace eng
