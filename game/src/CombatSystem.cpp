#include "CombatSystem.h"

#include "GameAssets.h"
#include "GameContext.h"
#include "combat/ActionStateSystem.h"
#include "combat/PoiseSystem.h"
#include "combat/StaminaSystem.h"

#include <eng/Physics.h>
#include <eng/Renderer.h>

namespace game {

void CombatSystem::init(GameContext& ctx)
{
    reloadPresentation(ctx);
    // Damage model: weapon table (weapons.toml overlays built-in defaults).
    // The channel names it uses are resolved against the world's vocabulary.
    mDirector.init(assetPath("config/weapons.toml"), ctx.vocabulary);
}

void CombatSystem::reloadPresentation(GameContext& ctx)
{
    mBlood.load(ctx.renderer, assetPath("config/blood.toml"));
}

void CombatSystem::fixedStep(GameContext& ctx, glm::vec3 eye, glm::vec3 forward,
                             float dt)
{
    mProjectiles.fixedUpdate(ctx.physics, ctx.renderer, dt);
    mDirector.tick(dt); // i-frames + status effects (Burn DoT) at fixed cadence

    // Feel layer: advance every combatant's action-state machine, then regen
    // stamina and poise (and count down stagger/immunity timers). Runs on the
    // director's registry each fixed step so timings stay deterministic.
    entt::registry& reg = mDirector.registry();
    feel::actionstate::advance(reg, dt);
    feel::stamina::tick(reg, dt);
    feel::poise::tick(reg, dt);
}

void CombatSystem::syncRender(GameContext& ctx)
{
    mProjectiles.syncRender(ctx.physics, ctx.renderer);
}

void CombatSystem::onContact(GameContext& ctx, const eng::HitEvent& e)
{
    mProjectiles.onHit(ctx.physics, ctx.renderer, e);
}

void CombatSystem::clear(GameContext& ctx)
{
    mProjectiles.clear(ctx.physics, ctx.renderer);
}

void CombatSystem::fireWeapon(GameContext& ctx, const PlayerWeaponDef& weapon,
                              glm::vec3 eye, glm::vec3 forward,
                              std::optional<glm::vec3> muzzle)
{
    mProjectiles.fire(ctx.physics, ctx.renderer, weapon, eye, forward, muzzle);
}

} // namespace game
