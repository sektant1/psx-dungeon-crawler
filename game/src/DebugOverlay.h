#pragma once
#include <entt/entt.hpp>

#include <eng/DebugTools.h> // eng::DebugTools, the console these panels join
#include <eng/debug/ParticlePanel.h> // Particles: engine library, engine panel
#include <eng/debug/SurfacePanels.h> // Portal/VFX: engine shaders, engine panels

#include <glm/glm.hpp>

#include <string>

namespace eng {
class Renderer;
class ParticleLibrary;
} // namespace eng

class LiveLevel;

namespace game {

class PlayerSystem;
class EnemySystem;
class EnemyLibrary;
class EnemySpawner;
class GameAudioSystem;
struct GameContext;

// Collider-view settings are engine tooling; the game just keeps naming the
// type `game::ColliderDebug` (eng::FpsGameApp owns the instance and the F3
// toggle).
using ColliderDebug = eng::ColliderDebug;

// The portal *prop's* dressing, which is level state rather than shader state:
// the glow the membrane throws into the room and the wisp effect drifting in
// front of it. The shader's own knobs are eng::PortalTuning -- they drive an
// engine shader, so they live with it and the demo can reach them too.
struct PortalDressing {
    glm::vec3 lightColour{0.22f, 1.05f, 0.10f};
    float lightRange = 8.5f;
    std::string wisps = "engine.portal_wisps";
};

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
        // Weapon presentation toggles live on the player system; the panel
        // needs the renderer to push a change through to the live viewmodels.
        PlayerSystem* playerSystem = nullptr;
        eng::Renderer* renderer = nullptr;

        // Enemy tuning. All four are needed together (spawning wants a world,
        // a table and a place to put it), so the tab shows a note unless the
        // whole set is present.
        EnemySystem* enemies = nullptr;
        EnemyLibrary* enemyLibrary = nullptr;
        EnemySpawner* spawner = nullptr;
        GameContext* context = nullptr;

        // Portal dressing: the live props (light, wisps) and the effect table
        // the wisp selector lists. Both are rebuilt on a level transition,
        // which is why they arrive per-frame like everything else here.
        LiveLevel* level = nullptr;
        eng::ParticleLibrary* particles = nullptr;
        GameAudioSystem* audio = nullptr;
        glm::vec3 playerFeet{0.0f};
        glm::vec3 playerForward{0.0f, 0.0f, 1.0f};
        // Frame delta, for the panels that own something with a lifetime. Only
        // the particle panel does: it retires the one-shots it spawned, and it
        // has to keep doing that while its tab is hidden or a tuning session
        // walks away holding every burst it ever fired.
        float dt = 0.0f;
    };

    void install(eng::DebugTools& console);
    void update(const Deps& deps)
    {
        mCur = deps;
        mSurfaces.setRenderer(deps.renderer);
        mParticlePanel.setSources(deps.renderer, deps.particles);
        mParticlePanel.update(deps.dt);
    }

    // Level transitions destroy the scene the panel's spawns live in. The
    // handles go stale harmlessly, but the panel's own list would keep growing
    // across every transition of a session.
    void releaseParticleSpawns() { mParticlePanel.releaseSpawns(); }

private:
    void drawCombatTab();
    void drawViewmodelTab();
    // The handles themselves, drawn over the game view rather than inside the
    // panel. Called from the Viewmodel tab so the controls and the gizmo they
    // configure cannot get out of step.
    void drawViewmodelGizmo();
    void drawFeelTab();
    void drawEnemiesTab();
    void drawAudioTab();
    // The Dressing section of the engine's Portal tab, called with the index of
    // the selected profile -- 1 is the ascent portal, matching the order the
    // materials are registered in install().
    void drawPortalDressing(int profileIdx);

    Deps mCur;
    // The Portal and VFX tabs themselves belong to the engine: they tune engine
    // shaders, and the demo stages the same materials. This class registers the
    // game's materials with them and adds the one section that is the game's.
    eng::SurfacePanels mSurfaces;
    // Likewise engine tooling: it edits eng::ParticleLibrary and spawns through
    // the Renderer, neither of which is a game concept.
    eng::ParticlePanel mParticlePanel;
    // Descent/ascent prop dressing, in the order install() registers the
    // materials.
    PortalDressing mDressing[2];
    // Enemies tab UI state: which definition the spawn button uses, and which
    // one the tuning sliders edit. Ids, not pointers: a library reload
    // invalidates definitions but not their names.
    std::string mSpawnId;
    std::string mTuneId;
    // Viewmodel tab: which definition the presentation sliders edit. It tracks
    // the equipped weapon by default -- the common case is tuning what you are
    // holding -- until the viewer picks a row by hand.
    int mViewmodelWeapon = 0;
    // Result of the last "Save to game.toml", and how long the panel still says
    // so. A save that reports nothing is a save you press twice.
    bool mViewmodelSaved = false;
    float mViewmodelSaveNoted = 0.0f;
    bool mViewmodelFollowsEquipped = true;
    // Viewmodel gizmo: which transform the handles drive, and how. The handles
    // sit on the *authored* pose rather than the animated node, so bob and
    // sway do not drag them out from under the cursor mid-drag.
    // Socket    -- the shared rig's camera-space placement (ViewmodelRig)
    // WeaponLean-- this weapon's nudge on top of it
    // WeaponAttach -- where the weapon sits inside the hand socket it hangs on
    // Muzzle    -- the point its shots leave from
    enum class ViewmodelGizmoTarget { Socket, WeaponLean, WeaponAttach, Muzzle };
    bool mGizmoEnabled = false;
    ViewmodelGizmoTarget mGizmoTarget = ViewmodelGizmoTarget::Socket;
    int mGizmoOperation = 0; // 0 translate, 1 rotate, 2 scale
    bool mGizmoLocal = true;
    bool mGizmoSnap = false;
    float mGizmoTranslateSnap = 0.01f;
    float mGizmoRotateSnap = 5.0f;
    // Motion is frozen for the duration of a drag and restored after it, so a
    // placement is made against a still pose without the viewer having to
    // remember to switch the layers off and back on.
    bool mGizmoFroze = false;
    bool mGizmoMotionWas = true;
};

} // namespace game
