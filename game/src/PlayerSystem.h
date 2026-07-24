#pragma once
#include "FpsController.h"
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

    // Locomotion (reads input, moves the character). Skip when driving a scripted
    // preview camera.
    void update(GameContext& ctx, float dt);
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

    FpsController& controller() { return mPlayer; }
    const FpsController& controller() const { return mPlayer; }

private:
    void applyWeaponVis(GameContext& ctx);

    FpsController mPlayer;
    ViewModel mSwordModel;
    ViewModel mStaffModel;
    ViewModel mTorchModel;
    int mWeapon = WSword;
    float mSpeed = 3.0f;
    float mSens = 0.002f;
};

} // namespace game
