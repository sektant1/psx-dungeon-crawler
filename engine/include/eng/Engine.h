#pragma once
#include <eng/Config.h>
#include <eng/Input.h>
#include <eng/Renderer.h>
#include <eng/StepClock.h>
#include <eng/System.h>

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
    bool init(const std::string& configPath, const std::string& appAssetDir);

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
    bool mClose = false;
    std::vector<System::StrongPtr> mSystems;
};

} // namespace eng
