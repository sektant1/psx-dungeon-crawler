#include "CombatSystem.h"

#include "GameContext.h"
#include "combat/ActionStateSystem.h"
#include "combat/PoiseSystem.h"
#include "combat/StaminaSystem.h"

#include <eng/Physics.h>
#include <eng/Renderer.h>

namespace game {

void CombatSystem::init(GameContext& ctx, const std::string& configTomlPath)
{
    // Data-oriented attack tunables (speed/range/impulse/colours/particles/
    // hotkeys), loaded from [combat.*]. Systems read this each cast/swing.
    mConfig.load(configTomlPath);
    mProjectiles.setConfig(&mConfig);
    mSpells.setConfig(&mConfig);
    mMelee.setConfig(&mConfig);
    // init builds procedural meshes and registers the contact seam.
    mProjectiles.init(ctx.renderer);
    mSpells.init(ctx.renderer);
    mMelee.setHitCallback([&ctx](eng::BodyHandle, glm::vec3 point, glm::vec3) {
        ctx.renderer.spawnParticles("engine.hit_sparks", point);
    });
    // Damage model: weapon table (weapons.toml overlays built-in defaults).
    mDirector.init(ctx.assets + "/weapons.toml");
}

void CombatSystem::fixedStep(GameContext& ctx, glm::vec3 eye, glm::vec3 forward,
                             float dt)
{
    mProjectiles.fixedUpdate(ctx.physics, ctx.renderer, dt);
    mSpells.fixedUpdate(ctx.physics, ctx.renderer, dt);
    mMelee.fixedUpdate(ctx.physics, eye, forward, dt);
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
    mSpells.syncRender(ctx.physics, ctx.renderer);
}

void CombatSystem::onContact(GameContext& ctx, const eng::HitEvent& e)
{
    mProjectiles.onHit(ctx.physics, e);
    mSpells.onHit(ctx.physics, ctx.renderer, e);
}

void CombatSystem::clear(GameContext& ctx)
{
    mProjectiles.clear(ctx.physics, ctx.renderer);
    mSpells.clear(ctx.physics, ctx.renderer);
}

void CombatSystem::fireArrow(GameContext& ctx, glm::vec3 eye, glm::vec3 forward)
{
    mProjectiles.fireArrow(ctx.physics, ctx.renderer, eye, forward);
}

void CombatSystem::castFireball(GameContext& ctx, glm::vec3 eye, glm::vec3 fwd)
{
    mSpells.castFireball(ctx.physics, ctx.renderer, eye, fwd);
}

void CombatSystem::castBeam(GameContext& ctx, glm::vec3 eye, glm::vec3 fwd)
{
    mSpells.castBeam(ctx.physics, ctx.renderer, eye, fwd);
}

} // namespace game
