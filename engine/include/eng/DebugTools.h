#pragma once
#include <eng/FrameStats.h>       // eng::FrameStatsView, plotted by the HUD
#include <eng/Physics.h>          // eng::CollisionMask, source of the collider lines
#include <eng/RenderPresetInfo.h> // eng::renderPresets(), listed by the Render tab
#include <eng/StepClock.h>        // eng::StepClock, edited by the Animation tab

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eng {

class Renderer;
class FpsController;

// Collider-debug view settings, owned by the application and edited from the
// console's Colliders tab (the application picks the toggle key; the game
// binds F3). Read by the application's collider-draw block.
struct ColliderDebug {
    bool enabled = false;                        // master on/off
    int colorMode = 0;                           // 0=by shape,1=by layer,2=uniform
    glm::vec3 uniformColor{0.95f, 0.10f, 0.75f}; // used when colorMode==uniform
    float brightness = 1.0f;                     // colour multiplier
    float thickness = 1.5f;                      // screen-space line width (px)
    bool includeStatic = false;                  // hide static level (walls/floor/ceiling) by default
    float range = 35.0f;                         // metres from the viewer; <= 0 draws all
    float fadeStart = 24.0f;                     // metres; <= 0 uses the outer third
    bool drawCharacters = true;
    bool drawSensors = true;
};

// The half of the collider view the engine cannot know: which layers count as
// "the static level" for the includeStatic toggle, and where the viewer is for
// the range fade. Everything else comes from ColliderDebug.
struct ColliderOverlayOptions {
    // Hidden when ColliderDebug::includeStatic is false. Left empty, the toggle
    // does nothing -- a game that has no static layer loses nothing.
    CollisionMask staticLayers = kNoLayers;
    glm::vec3 viewer{0.0f};    // usually the eye position
    float sweepDt = 1.0f / 60.0f; // matches the sim step, for swept shapes
};

// Draws every collider as a SCREEN-SPACE imgui overlay: project each 3D line to
// the window at full resolution, so the wireframe stays crisp and identical no
// matter what the render profile does to the framebuffer (pixelation, dither,
// downscale). Call inside the app's imgui frame; no-op when view.enabled is
// false.
//
// This lives in the engine because none of it is a game decision -- it is
// Physics::debugDraw plus a view-projection and a near-plane cull, and every
// game that reimplements it reimplements the same near-plane bug.
void drawColliderOverlay(Physics& physics, Renderer& renderer,
                         const ColliderDebug& view,
                         const ColliderOverlayOptions& options);

// On-screen Dear ImGui debug/tuning console: a single panel docked to the
// right edge of the window, split into one tab per tweakable subsystem. Every
// control writes straight through to the live system (renderer setters, step
// clock, debug-view structs) so edits take effect on the next frame -- no
// apply button.
//
// The engine owns the tabs that tune the engine: render profile, stylize
// shaders, step clock, colliders, materials, player controller + frame stats.
// An application adds its own tabs with addPanel(); the engine never learns
// what they contain.
//
// The console owns UI-side cache only (render-profile selection + the editable
// preset values that make a profile fully customizable/reproducible); all real
// state lives in the systems it edits. Build once, toggle with the app's key,
// and call draw() every frame it is visible (between Engine::beginImGuiFrame
// and Engine::renderFrame). Ownership of referenced systems stays with the
// application; draw() receives them per-frame via Deps so nothing dangles
// across level rebuilds.
class DebugTools {
public:
    // Per-frame wiring. Any pointer may be null; the matching tab then shows a
    // "not available" note instead of dereferencing it.
    struct Deps {
        Renderer* renderer = nullptr;
        FpsController* fps = nullptr;
        const FrameStatsView* frame = nullptr;
        ColliderDebug* colliders = nullptr; // collider-view settings
        StepClock* steps = nullptr;         // stop-motion animation rates
        // The render profile the engine actually applied at startup. The panel
        // seeds its editable copy from this: seeded from the *default* instead,
        // every slider pushed a value from a profile that was not running, so
        // the first touch of any control snapped the look somewhere else and
        // the panel read as broken.
        int renderPresetId = 0;
    };

    DebugTools();
    ~DebugTools();
    DebugTools(DebugTools&&) noexcept;
    DebugTools& operator=(DebugTools&&) noexcept;

    // Register an application tab, drawn after the engine's own. `draw` runs
    // inside the tab item, once per frame the tab is selected. Call once at
    // startup: the callback should read whatever it needs through pointers the
    // application refreshes, so nothing dangles across level rebuilds.
    void addPanel(std::string name, std::function<void()> draw);

    bool visible() const { return mVisible; }
    void setVisible(bool v) { mVisible = v; }
    void toggle() { mVisible = !mVisible; }

    // Builds the docked panel. No-op when hidden. Safe to call every frame.
    void draw(const Deps& deps);

private:
    void drawRenderTab(const Deps& d);
    void drawAnimationTab(const Deps& d);
    void drawShadersTab(const Deps& d);
    void drawCollidersTab(const Deps& d);
    void drawPlayerTab(const Deps& d);
    void drawMaterialsTab(const Deps& d);
    void loadProfile(const Deps& d, int id);

    struct Panel {
        std::string name;
        std::function<void()> draw;
    };

    // Editable copy of the active render profile and the rest of the UI-side
    // cache. Held out of line so the preset format stays an engine-private
    // type: nothing outside the engine has a reason to name it.
    struct State;
    std::unique_ptr<State> mState;
    std::vector<Panel> mPanels;
    bool mVisible = false;
};

// Always-on performance HUD: a small top-left window with FPS, frame-time
// graph, per-phase CPU breakdown, and draw-call/triangle counts. Independent of
// the debug console (starts visible; the application binds a toggle -- the game
// uses F4). Draw it every frame inside the same imgui frame as the console.
class PerfOverlay {
public:
    bool visible() const { return mVisible; }
    void setVisible(bool v) { mVisible = v; }
    void toggle() { mVisible = !mVisible; }
    void draw(const FrameStatsView* frame, Renderer* renderer);

private:
    bool mVisible = true;
};

} // namespace eng
