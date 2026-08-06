#pragma once
#include "BloodSystem.h"
#include "PlayerWeapons.h"
#include "Projectiles.h"
#include "WeaponDelivery.h"
#include "combat/CombatDirector.h"
#include "combat/CombatVocabulary.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>

namespace eng { struct HitEvent; }

namespace game {

struct GameContext;

// Owns generic player weapon delivery and the shared damage model.
//
// Three deliveries, selected per weapon by data (WeaponFireMode): projectiles
// spawn bodies and are reconciled every frame, melee sweeps a shape over a
// timed window, hitscan resolves a ray at once. They are two objects rather
// than one because their lifetimes are opposites, and one damage path because
// they report through the same impact callback -- everything downstream of a
// hit sees an event, never a delivery.
class CombatSystem {
public:
    void init(GameContext& ctx);
    void reloadPresentation(GameContext& ctx);

    // Advance all attack subsystems inside the fixed physics substep.
    void fixedStep(GameContext& ctx, glm::vec3 eye, glm::vec3 forward, float dt);
    // Reconcile projectile render nodes after the substep.
    void syncRender(GameContext& ctx);
    // Route a physics contact to player projectiles.
    void onContact(GameContext& ctx, const eng::HitEvent& e);
    // Free all live projectiles (level transition / shutdown).
    void clear(GameContext& ctx);

    void fireWeapon(GameContext& ctx, const PlayerWeaponDef& weapon,
                    glm::vec3 eye, glm::vec3 forward,
                    std::optional<glm::vec3> muzzle = std::nullopt);
    ProjectileSystem& projectiles() { return mProjectiles; }
    // Melee swings and hitscan rays. Exposed for the same reason projectiles
    // are: main.cpp installs the impact callback that turns a hit into damage,
    // audio and hit feel, and both deliveries must reach the same one.
    WeaponDeliverySystem& delivery() { return mDelivery; }

    BloodSystem& blood() { return mBlood; }
    const BloodSystem& blood() const { return mBlood; }
    // Damage model: HP/resistances/status effects + weapon table. Gameplay
    // registers combatants and routes hits through this.
    CombatDirector& director() { return mDirector; }

    // Advance the damage model (i-frames + status effects). Call once per fixed
    // step, alongside fixedStep.
    void tickDirector(float dt) { mDirector.tick(dt); }

private:
    ProjectileSystem mProjectiles;
    WeaponDeliverySystem mDelivery;
    CombatDirector mDirector;
    BloodSystem mBlood;
};

} // namespace game
