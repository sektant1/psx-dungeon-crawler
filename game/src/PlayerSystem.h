#pragma once
#include <eng/controllers/FpsController.h>
#include <eng/ecs/components/FirstPersonController.h>
#include "PlayerWeapons.h"
#include "FirstPersonHands.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

namespace game {

struct GameContext;

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
    // (Re)attach the carried light + viewmodels to the fresh head node (the old
    // one is destroyed by clearScene) and apply active-weapon visibility.
    void attachLoadout(GameContext& ctx);

    // Mouse look, once per rendered frame, at the render rate: quantising the
    // view to the simulation rate reads as input lag.
    void look(GameContext& ctx);
    // Locomotion, inside the fixed-step loop. Running the character controller
    // on the render delta made movement frame-rate dependent and unstable
    // against the fixed-rate world around it.
    void fixedStep(GameContext& ctx, float fixedDt);
    // Push the interpolated pose to the renderer. `alpha` is the fraction
    // between the last two fixed steps (Physics::interpolationAlpha).
    void present(GameContext& ctx, float alpha);
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
    // re-poses the rig so a frozen viewmodel still tracks the sliders.
    void refreshViewmodel(GameContext& ctx);

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
    eng::FpsController mPlayer;
    PlayerWeaponLibrary mWeaponLibrary;
    WeaponController mWeapons;
    FirstPersonHands mHands;
    eng::ecs::FirstPersonController mTuning{};
    float mFootstepFxCooldown = 0.0f;
    glm::vec2 mLastLookDelta{0.0f};
    bool mWeaponInputWasEnabled = false;
};

} // namespace game
