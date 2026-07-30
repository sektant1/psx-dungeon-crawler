#pragma once
#include <eng/controllers/FpsController.h>
#include "ViewModel.h"

#include <glm/glm.hpp>

namespace game {

struct GameContext;

// Owns the player controller, the three first-person viewmodels, and the active
// weapon selection. Weapon choice sits here (not in CombatSystem) because it
// drives which viewmodel is shown; CombatSystem reads the equipped weapon to
// gate casts/swings. Locomotion and viewmodel animation are advanced from the
// loop at their existing phases.
class PlayerSystem {
public:
    enum Weapon { WSword = 0, WStaff = 1, WTorch = 2, WeaponCount = 3 };

    // Movement tunables, read once from config before the first spawn.
    void setTuning(float speed, float sensitivity) {
        mSpeed = speed;
        mSens = sensitivity;
    }

    // (Re)spawn the player controller at pos (fresh body/head nodes). Call
    // attachLoadout afterwards; callers may drive preview view angles in between.
    void spawnAt(GameContext& ctx, glm::vec3 pos);
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
    // Advance the viewmodels. attackTriggered = a melee click this frame;
    // didCast = a spell was cast this frame; aiming = parry/aim held.
    void updateViewmodels(GameContext& ctx, float dt, bool attackTriggered,
                          bool didCast, bool aiming);

    // Cycle the active weapon and refresh viewmodel visibility.
    void swapWeapon(GameContext& ctx);

    int weapon() const { return mWeapon; }
    bool swordEquipped() const { return mWeapon == WSword; }
    bool staffEquipped() const { return mWeapon == WStaff; }
    bool torchEquipped() const { return mWeapon == WTorch; }

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
    ViewModel mSwordModel;
    ViewModel mStaffModel;
    ViewModel mTorchModel;
    int mWeapon = WSword;
    bool mWeaponEnchant = false;
    float mSpeed = 3.0f;
    float mSens = 0.002f;
    float mFootstepFxCooldown = 0.0f;
};

} // namespace game
