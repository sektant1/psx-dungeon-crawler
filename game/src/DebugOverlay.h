#pragma once
#include <entt/entt.hpp>

#include <eng/DebugTools.h> // eng::DebugTools, the console these panels join

namespace eng { class Renderer; }

struct CombatConfig;

namespace game {

class PlayerSystem;

// Collider-view settings are engine tooling; the game just keeps naming the
// type `game::ColliderDebug` (eng::FpsGameApp owns the instance and the F3
// toggle).
using ColliderDebug = eng::ColliderDebug;

// The two debug tabs that are game policy: Combat (weapon/projectile tuning)
// and Feel (stamina, poise, action state). Everything that tunes the *engine*
// -- render profile, stylize shaders, step clock, colliders, player controller,
// materials, frame stats -- is eng::DebugTools' own, so this class is only the
// part the engine must not know about.
//
// install() once at startup, then update() every frame with live pointers: the
// combat registry and player entity are recreated on level transitions, so the
// panels read them indirectly rather than capturing them.
class DebugPanels {
public:
    struct Deps {
        CombatConfig* combat = nullptr;
        entt::registry* registry = nullptr; // combat director registry
        entt::entity player = entt::null;   // player entity in `registry`
        // Weapon presentation toggles live on the player system; the panel needs
        // the renderer to push a change through to the live viewmodels.
        PlayerSystem* playerSystem = nullptr;
        eng::Renderer* renderer = nullptr;
    };

    void install(eng::DebugTools& console);
    void update(const Deps& deps) { mCur = deps; }

private:
    void drawCombatTab();
    void drawFeelTab();

    Deps mCur;
};

} // namespace game
