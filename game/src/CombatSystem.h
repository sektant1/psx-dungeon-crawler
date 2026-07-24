#pragma once
#include "CombatConfig.h"
#include "Melee.h"
#include "Projectiles.h"
#include "Spells.h"

#include <glm/glm.hpp>

#include <string>

namespace eng { struct HitEvent; }

namespace game {

struct GameContext;

// Owns the three attack subsystems (arrows, spells, melee) and their shared
// data-driven CombatConfig. Consolidates the lifecycle calls that were
// scattered across the main loop (fixed-step, render sync, contact routing,
// teardown) behind one combat owner. Weapon selection and viewmodels stay with
// the player; this only fires and simulates.
class CombatSystem {
public:
    // Load combat.toml tunables and build procedural projectile/spell meshes.
    void init(GameContext& ctx, const std::string& configTomlPath);

    // Advance all attack subsystems inside the fixed physics substep.
    void fixedStep(GameContext& ctx, glm::vec3 eye, glm::vec3 forward, float dt);
    // Reconcile projectile/spell render nodes after the substep.
    void syncRender(GameContext& ctx);
    // Route a physics contact to the projectile and spell systems.
    void onContact(GameContext& ctx, const eng::HitEvent& e);
    // Free all live projectiles/spells (level transition / shutdown).
    void clear(GameContext& ctx);

    // --- fire actions (called from input handling) ---
    void fireArrow(GameContext& ctx, glm::vec3 eye, glm::vec3 forward);
    void castFireball(GameContext& ctx, glm::vec3 eye, glm::vec3 forward);
    void castBeam(GameContext& ctx, glm::vec3 eye, glm::vec3 forward);
    void startSwing() { mMelee.startSwing(); }

    MeleeSystem& melee() { return mMelee; }        // hit-callback wiring
    CombatConfig& config() { return mConfig; }     // debug UI
    const CombatConfig& config() const { return mConfig; }

private:
    ProjectileSystem mProjectiles;
    SpellSystem mSpells;
    MeleeSystem mMelee;
    CombatConfig mConfig;
};

} // namespace game
