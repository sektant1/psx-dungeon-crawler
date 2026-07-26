#pragma once
#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "RenderPresets.h" // eng::RenderPresetValues editable profile cache
#include <eng/StepClock.h> // eng::StepChannel/StepRates in the Animation tab

namespace eng { class Renderer; }

struct CombatConfig;
class FpsController; // global namespace (game/src/FpsController.h)

namespace game {

struct ProfHud;

// Collider-debug view settings, owned by main.cpp and edited from the console's
// Colliders tab (F3 toggles `enabled`). Read by the loop's collider-draw block.
struct ColliderDebug {
    bool enabled = false;                       // master on/off (F3)
    int colorMode = 0;                          // 0=by shape,1=by layer,2=uniform
    glm::vec3 uniformColor{0.95f, 0.10f, 0.75f}; // used when colorMode==uniform
    float brightness = 1.0f;                     // colour multiplier
    float thickness = 1.5f;                      // screen-space line width (px)
    bool includeStatic = false;                  // hide static level (walls/floor/ceiling) by default
};

// On-screen Dear ImGui debug/tuning console. A single panel docked to the right
// edge of the window, split into one tab per tweakable subsystem. Every control
// writes straight through to the live system (renderer setters, config structs,
// ECS components) so edits take effect on the next frame -- no apply button.
//
// The panel owns UI-side cache only (render-profile selection + the editable
// RenderPresetValues that makes a profile fully customizable/reproducible); all
// real state lives in the systems it edits. Build once, toggle with F1, and call
// draw() every frame the overlay is visible (between Engine::beginImGuiFrame and
// Engine::renderFrame). Ownership of referenced systems stays with main.cpp;
// draw() receives them per-frame via Deps so nothing dangles across level
// rebuilds (the combat registry/player entity are recreated on descent).
class DebugOverlay {
public:
    // Per-frame wiring. Any pointer may be null; the matching tab then shows a
    // "not available" note instead of dereferencing it.
    struct Deps {
        eng::Renderer* renderer = nullptr;
        CombatConfig* combat = nullptr;
        ::FpsController* fps = nullptr;
        entt::registry* registry = nullptr; // combat director registry
        entt::entity player = entt::null;    // player entity in `registry`
        const ProfHud* prof = nullptr;
        ColliderDebug* colliders = nullptr;  // F3 collider-view settings
        eng::StepClock* steps = nullptr;     // stop-motion animation rates
    };

    bool visible() const { return mVisible; }
    void setVisible(bool v) { mVisible = v; }
    void toggle() { mVisible = !mVisible; }

    // Builds the docked panel. No-op when hidden. Safe to call every frame.
    void draw(const Deps& deps);

private:
    void drawRenderTab(const Deps& d);
    void drawAnimationTab(const Deps& d);
    void drawShadersTab(const Deps& d);
    void drawCombatTab(const Deps& d);
    void drawFeelTab(const Deps& d);
    void drawPlayerTab(const Deps& d);
    void drawCollidersTab(const Deps& d);
    void drawMaterialsTab(const Deps& d);

    // Load preset `id` (1..6) into the editable cache and push it to the renderer.
    void loadProfile(const Deps& d, int id);

    bool mVisible = false;
    // Render-profile combo index (0-based; id = index + 1). Starts on the
    // engine default so the panel opens showing the look that is actually live.
    int mPreset = eng::kDefaultRenderPreset - 1; // dungeon
    bool mProfileInit = false; // mRp seeded from the initial profile yet?
    // Editable copy of the active render profile. Every Render/Shaders slider
    // edits a field here and pushes just that field to the renderer, so a full
    // profile can be tuned live and dumped back out (Copy as TOML) reproducibly.
    eng::RenderPresetValues mRp;

    int mMaterialIdx = 0;                    // materials tab: selected material row
    char mParamName[64] = "outlineOpacity";  // materials tab: global-param entry
    float mParamValue = 0.26f;
};

// Always-on performance HUD: a small top-left window with FPS, frame-time graph,
// per-phase CPU breakdown, and draw-call/triangle counts. Independent of the
// debug console (starts visible, toggled with F4). Draw it every frame inside
// the same imgui frame as the console.
class PerfOverlay {
public:
    bool visible() const { return mVisible; }
    void setVisible(bool v) { mVisible = v; }
    void toggle() { mVisible = !mVisible; }
    void draw(const ProfHud* prof, eng::Renderer* renderer);

private:
    bool mVisible = true;
};

} // namespace game
