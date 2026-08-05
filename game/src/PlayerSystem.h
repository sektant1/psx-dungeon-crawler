#pragma once
#include <eng/Primitive.h>
#include <eng/camera/ThirdPersonCameraRig.h>
#include <eng/controllers/FpsController.h>
#include <eng/ecs/components/FirstPersonController.h>
#include <eng/ecs/components/ThirdPersonCamera.h>
#include "LockOn.h"
#include "PlayerWeapons.h"
#include "FirstPersonHands.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

namespace game {

struct GameContext;

// Which camera shape the player is being played through. The two are the same
// character, the same capsule and the same weapons -- what differs is where the
// view sits, and therefore whether the hands or the body is what you see.
enum class CameraMode { FirstPerson, ThirdPerson };

// Owns player locomotion plus data-driven ranged loadout/runtime presentation.
class PlayerSystem {
public:
    // How the player moves and what the lens does. The same struct the scene
    // format authors on a camera (eng::ecs::FirstPersonController), so a
    // level's override and the game's config defaults arrive by one path.
    // Applied to the live controller immediately and again at the next spawn,
    // which recreates it.
    void setControllerTuning(const eng::ecs::FirstPersonController& tuning);
    const eng::ecs::FirstPersonController& controllerTuning() const
    {
        return mTuning;
    }

    // (Re)spawn the player controller at pos (fresh body/head nodes). Call
    // attachLoadout afterwards; callers may drive preview view angles in between.
    void spawnAt(GameContext& ctx, glm::vec3 pos);
    bool loadWeapons(const std::string& definitionsPath);
    // The hands rig and its socket vocabulary (assets/config/viewmodel_hands.toml).
    // Read before attachLoadout; a missing or rejected file keeps the shipped
    // arms rather than leaving the player with nothing to hold a weapon with.
    bool loadHands(const std::string& definitionsPath);
    // (Re)attach the carried light + viewmodels to the fresh head node (the old
    // one is destroyed by clearScene) and apply active-weapon visibility.
    void attachLoadout(GameContext& ctx);

    // --- camera ----------------------------------------------------------
    // The over-the-shoulder framing, from config or from the level's authored
    // ThirdPersonCamera. Applied to the live rig immediately; the mode itself
    // is a separate call, so a level can supply numbers without switching.
    void setCameraTuning(const eng::ecs::ThirdPersonCamera& tuning);
    const eng::ecs::ThirdPersonCamera& cameraTuning() const { return mCamera; }
    // Swap the camera shape. Live: the outgoing rig gives up its nodes and the
    // loadout is re-seated on the new eye, so this works mid-level as a toggle
    // as well as at load time.
    void setCameraMode(GameContext& ctx, CameraMode mode);
    // The same choice, made before the player exists: sets the mode and
    // nothing else, for the window between a scene clear and the next spawnAt
    // (which applies it). Touching the renderer in there would work on nodes
    // the scene clear has already destroyed.
    void selectCameraMode(CameraMode mode) { mMode = mode; }
    CameraMode cameraMode() const { return mMode; }

    // Lock-on. The system owns the decision; this pushes its result into the
    // camera (what to frame) and the controller (what to face), which is the
    // only place those three meet.
    LockOnSystem& lockOn() { return mLockOn; }
    const LockOnSystem& lockOn() const { return mLockOn; }
    void applyLockOn(GameContext& ctx);

    // Where a shot starts and where it goes. The eye and the view direction in
    // first person; in third person the character's own head, aimed at the lock
    // if there is one -- never the boom, which is metres behind a wall as often
    // as not.
    glm::vec3 aimOrigin() const;
    glm::vec3 aimDirection() const;

    // Mouse look, once per rendered frame, at the render rate: quantising the
    // view to the simulation rate reads as input lag.
    void look(GameContext& ctx);
    // Locomotion, inside the fixed-step loop. Running the character controller
    // on the render delta made movement frame-rate dependent and unstable
    // against the fixed-rate world around it.
    void fixedStep(GameContext& ctx, float fixedDt);
    // Push the interpolated pose to the renderer. `alpha` is the fraction
    // between the last two fixed steps (Physics::interpolationAlpha), `frameDt`
    // the real frame delta the camera rig eases its own layers on.
    void present(GameContext& ctx, float alpha, float frameDt = 0.0f);
    // Render input is sampled once, then consumed by fixed simulation so catch-up
    // steps cannot duplicate click/swap edges.
    void sampleWeaponInput(GameContext& ctx, bool enabled);
    std::optional<std::size_t> fixedStepWeapons(GameContext& ctx, Mana& arc,
                                                bool canFire, float fixedDt);
    // `animationDt` is the stepped viewmodel channel, `frameDt` the real frame
    // delta the procedural rig motion runs on. See FirstPersonHands::update.
    void updateViewmodels(GameContext& ctx, float animationDt, float frameDt);
    std::optional<glm::vec3>
    projectileMuzzle(const eng::Renderer& renderer) const;

    // Shared first-person rig placement/feel. Authored in game.toml's
    // [player_viewmodel]; the Viewmodel debug panel edits it in place.
    void setViewmodelRig(const ViewmodelRig& tuning);
    ViewmodelRig& viewmodelRig() { return mHands.rig(); }
    const ViewmodelRig& viewmodelRig() const { return mHands.rig(); }
    FirstPersonHands& hands() { return mHands; }
    const FirstPersonHands& hands() const { return mHands; }
    // Re-applies the selected weapon's feel numbers after a live edit, and
    // re-poses the rig so a frozen viewmodel still tracks the sliders. Also
    // re-seats the held weapon, so dragging its attach offset moves it.
    void refreshViewmodel(GameContext& ctx);
    // Rebuilds the held weapon's geometry. Needed only when the presentation
    // itself changed -- a different model, or a different socket -- not for the
    // offsets, which refreshViewmodel handles without a reload.
    void rebuildWeaponViewmodel(GameContext& ctx);

    int weapon() const { return int(mWeapons.selectedIndex()); }
    const PlayerWeaponDef* selectedWeapon() const { return mWeapons.selected(); }
    const PlayerWeaponDef* weaponDefinition(std::size_t index) const;
    std::vector<PlayerWeaponDef>& weaponDefinitions() { return mWeaponLibrary.defs(); }
    const std::vector<PlayerWeaponDef>& weaponDefinitions() const {
        return mWeaponLibrary.defs();
    }

    eng::FpsController& controller() { return mPlayer; }
    const eng::FpsController& controller() const { return mPlayer; }

private:
    // Applies the current mode to the controller: which rig, which sensitivity,
    // which FOV, whether the body follows the view or its own travel, and
    // whether the hands or the avatar is the thing on screen.
    void applyCameraMode(GameContext& ctx);
    void rebuildAvatar(GameContext& ctx);

    eng::FpsController mPlayer;
    eng::ThirdPersonCameraRig mThirdPerson;
    eng::ecs::ThirdPersonCamera mCamera{};
    CameraMode mMode = CameraMode::FirstPerson;
    LockOnSystem mLockOn;
    // The placeholder body you see in third person. A primitive rather than a
    // model because there is no player mesh in this project yet, and a camera
    // that frames nothing cannot be judged at all.
    eng::PrimitiveInstance mAvatar{};
    PlayerWeaponLibrary mWeaponLibrary;
    WeaponController mWeapons;
    FirstPersonHands mHands;
    HandsDefinition mHandsDefinition = defaultHandsDefinition();
    eng::ecs::FirstPersonController mTuning{};
    float mFootstepFxCooldown = 0.0f;
    glm::vec2 mLastLookDelta{0.0f};
    bool mWeaponInputWasEnabled = false;
};

} // namespace game
