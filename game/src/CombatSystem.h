#pragma once
#include "BloodSystem.h"
#include "PlayerWeapons.h"
#include "Projectiles.h"
#include "combat/CombatDirector.h"
#include "combat/CombatVocabulary.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>

namespace eng { struct HitEvent; }

namespace game {

struct GameContext;

// Owns generic player projectile delivery and shared damage model. Archived
// melee/spell prototypes remain in source but no longer participate here.
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
    CombatDirector mDirector;
    BloodSystem mBlood;
};

} // namespace game
