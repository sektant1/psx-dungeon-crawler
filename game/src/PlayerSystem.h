#pragma once
#include <eng/controllers/FpsController.h>
#include "PlayerWeapons.h"
#include "ViewModel.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

namespace game {

struct GameContext;

// Owns player locomotion plus data-driven ranged loadout/runtime presentation.
class PlayerSystem {
public:
    // Movement tunables, read once from config before the first spawn.
    void setTuning(float speed, float sensitivity) {
        mSpeed = speed;
        mSens = sensitivity;
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
    void updateViewmodels(GameContext& ctx, float dt);

    int weapon() const { return int(mWeapons.selectedIndex()); }
    const PlayerWeaponDef* selectedWeapon() const { return mWeapons.selected(); }
    const PlayerWeaponDef* weaponDefinition(std::size_t index) const;
    std::vector<PlayerWeaponDef>& weaponDefinitions() { return mWeaponLibrary.defs(); }
    const std::vector<PlayerWeaponDef>& weaponDefinitions() const {
        return mWeaponLibrary.defs();
    }

    // Weapon enchantment glow, across every viewmodel. Off by default: the glow
    // is presentation, and a plain weapon is the honest baseline for tuning
    // lighting and materials. The debug console drives this; it survives level
    // transitions because attachLoadout re-applies it to the rebuilt viewmodels.
    void setWeaponEnchant(eng::Renderer& r, bool on);
    bool weaponEnchant() const { return mWeaponEnchant; }

    eng::FpsController& controller() { return mPlayer; }
    const eng::FpsController& controller() const { return mPlayer; }

private:
    void applyWeaponVis(GameContext& ctx);

    eng::FpsController mPlayer;
    PlayerWeaponLibrary mWeaponLibrary;
    WeaponController mWeapons;
    std::vector<ViewModel> mViewmodels;
    std::vector<bool> mPendingFireAnimation;
    bool mWeaponEnchant = false;
    float mSpeed = 3.0f;
    float mSens = 0.002f;
    float mFootstepFxCooldown = 0.0f;
    glm::vec2 mLastLookDelta{0.0f};
    bool mWeaponInputWasEnabled = false;
};

} // namespace game
