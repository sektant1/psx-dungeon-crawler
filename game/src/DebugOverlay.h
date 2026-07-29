#pragma once
#include <entt/entt.hpp>

#include <eng/DebugTools.h> // eng::DebugTools, eng::PerfOverlay, eng::ColliderDebug
#include <eng/StepClock.h>  // eng::StepClock, edited by DebugTools' Animation tab

namespace eng { class Renderer; }

struct CombatConfig;
namespace eng { class FpsController; }

namespace game {

struct ProfHud;

// Collider-view settings are engine tooling now; the game just keeps naming the
// type `game::ColliderDebug` (F3 toggles `enabled`, read by the loop's
// collider-draw block).
using ColliderDebug = eng::ColliderDebug;

// The game's debug console. Everything tab-shaped that tunes the *engine*
// (render profile, stylize shaders, step clock, colliders, player controller,
// materials, frame stats) lives in eng::DebugTools now. This class owns that,
// and adds the two tabs that are game policy -- Combat and Feel -- through
// DebugTools' panel-registration seam, so the engine never learns what a
// "weapon" or a "poise bar" is.
//
// Build once, toggle with F1, and call draw() every frame the overlay is
// visible (between Engine::beginImGuiFrame and Engine::renderFrame). Ownership
// of referenced systems stays with main.cpp; draw() receives them per-frame via
// Deps so nothing dangles across level rebuilds (the combat registry/player
// entity are recreated on descent).
class DebugOverlay {
public:
    // Per-frame wiring. Any pointer may be null; the matching tab then shows a
    // "not available" note instead of dereferencing it.
    struct Deps {
        eng::Renderer* renderer = nullptr;
        CombatConfig* combat = nullptr;
        eng::FpsController* fps = nullptr;
        entt::registry* registry = nullptr; // combat director registry
        entt::entity player = entt::null;    // player entity in `registry`
        const ProfHud* prof = nullptr;
        ColliderDebug* colliders = nullptr;  // F3 collider-view settings
        eng::StepClock* steps = nullptr;     // stop-motion animation rates
    };

    DebugOverlay();

    bool visible() const { return mTools.visible(); }
    void setVisible(bool v) { mTools.setVisible(v); }
    void toggle() { mTools.toggle(); }

    // Builds the docked panel. No-op when hidden. Safe to call every frame.
    void draw(const Deps& deps);

private:
    void drawCombatTab();
    void drawFeelTab();

    eng::DebugTools mTools;
    // The Combat/Feel panels are registered once but read live gameplay state,
    // so draw() refreshes this each frame and the panel callbacks read from it.
    Deps mCur;
};

// Always-on performance HUD (F4). Thin adapter over eng::PerfOverlay that feeds
// it the game's ProfHud through the engine's phase-agnostic FrameStatsView.
class PerfOverlay {
public:
    bool visible() const { return mImpl.visible(); }
    void setVisible(bool v) { mImpl.setVisible(v); }
    void toggle() { mImpl.toggle(); }
    void draw(const ProfHud* prof, eng::Renderer* renderer);

private:
    eng::PerfOverlay mImpl;
};

} // namespace game
