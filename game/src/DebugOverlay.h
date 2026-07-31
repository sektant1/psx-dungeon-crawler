#pragma once
#include <entt/entt.hpp>

#include <eng/DebugTools.h> // eng::DebugTools, the console these panels join

#include <glm/glm.hpp>

#include <string>

namespace eng { class Renderer; }

namespace game {

class PlayerSystem;
class EnemySystem;
class EnemyLibrary;
class EnemySpawner;
struct GameContext;

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
        entt::registry* registry = nullptr; // combat director registry
        entt::entity player = entt::null;   // player entity in `registry`
        // Weapon presentation toggles live on the player system; the panel needs
        // the renderer to push a change through to the live viewmodels.
        PlayerSystem* playerSystem = nullptr;
        eng::Renderer* renderer = nullptr;

        // Enemy tuning. All four are needed together (spawning wants a world,
        // a table and a place to put it), so the tab shows a note unless the
        // whole set is present.
        EnemySystem* enemies = nullptr;
        EnemyLibrary* enemyLibrary = nullptr;
        EnemySpawner* spawner = nullptr;
        GameContext* context = nullptr;
        glm::vec3 playerFeet{0.0f};
        glm::vec3 playerForward{0.0f, 0.0f, 1.0f};
    };

    void install(eng::DebugTools& console);
    void update(const Deps& deps) { mCur = deps; }

private:
    void drawCombatTab();
    void drawFeelTab();
    void drawEnemiesTab();

    Deps mCur;
    // Enemies tab UI state: which definition the spawn button uses, and which
    // one the tuning sliders edit. Ids, not pointers: a library reload
    // invalidates definitions but not their names.
    std::string mSpawnId;
    std::string mTuneId;
};

} // namespace game
