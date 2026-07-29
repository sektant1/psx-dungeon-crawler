#include "PlayerSystem.h"

#include "GameContext.h"

#include <eng/LightDesc.h>
#include <eng/Renderer.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace game {

void PlayerSystem::spawnAt(GameContext& ctx, glm::vec3 pos)
{
    mPlayer.init(ctx.renderer, ctx.physics, pos, mSpeed, mSens,
                 glm::vec3(-1000.0f), glm::vec3(1000.0f));
    mPlayer.setCeilingHeight(3.0f);
}

void PlayerSystem::attachLoadout(GameContext& ctx)
{
    eng::Renderer& r = ctx.renderer;
    const std::string& assets = ctx.assets;
    // Warm carried light (sRGB-linearised, energy pre-multiplied).
    eng::LightDesc carry;
    carry.colour = glm::vec3(std::pow(1.0f, 2.2f), std::pow(0.80f, 2.2f),
                             std::pow(0.58f, 2.2f)) * 0.95f;
    carry.range = 7.0f;
    r.attachLight(mPlayer.headNode(), carry);
    // Weapon glows name schools; magic.toml decides what they look like.
    const CombatVocabulary& vocab = ctx.vocabulary;
    mSwordModel.init(r, mPlayer.headNode(), assets + "/meshes/props",
                     {vocab.palette("arcane"), 0.55f});
    mStaffModel.initStaff(r, mPlayer.headNode(),
                          assets + "/meshes/crystal_spire1.obj",
                          {vocab.palette("frost"), 0.85f});
    mTorchModel.initTorch(r, mPlayer.headNode(),
                          {vocab.palette("fire"), 0.35f});
    applyWeaponVis(ctx);
}

void PlayerSystem::look(GameContext& ctx)
{
    mPlayer.applyLook(eng::FpsController::readCommand(ctx.input));
}

void PlayerSystem::present(GameContext& ctx, float alpha)
{
    mPlayer.present(ctx.renderer, alpha);
}

void PlayerSystem::fixedStep(GameContext& ctx, float dt)
{
    // Look is applied per rendered frame; the command is re-read here for the
    // movement bits, whose edges (jump, slide) are consumed by the step.
    eng::FpsController::Command command =
        eng::FpsController::readCommand(ctx.input);
    command.mouseLook = false; // already applied this frame
    mPlayer.simulate(command, dt);
    mFootstepFxCooldown = std::max(0.0f, mFootstepFxCooldown - dt);
    if (mPlayer.grounded() && mPlayer.horizontalSpeed() > 1.2f &&
        mFootstepFxCooldown <= 0.0f) {
        ctx.renderer.spawnParticles(
            "engine.footstep_dust",
            mPlayer.position() + glm::vec3(0.0f, 0.03f, 0.0f));
        mFootstepFxCooldown = mPlayer.sprinting() ? 0.20f : 0.32f;
    }
}

void PlayerSystem::updateViewmodels(GameContext& ctx, float dt,
                                    bool attackTriggered, bool didCast,
                                    bool aiming)
{
    eng::Renderer& r = ctx.renderer;
    mSwordModel.update(r, dt, mWeapon == WSword && attackTriggered, aiming);
    mStaffModel.update(r, dt, didCast, false);
    mTorchModel.update(r, dt, mWeapon == WTorch && attackTriggered, false);
}

void PlayerSystem::swapWeapon(GameContext& ctx)
{
    mWeapon = (mWeapon + 1) % WeaponCount;
    applyWeaponVis(ctx);
}

void PlayerSystem::applyWeaponVis(GameContext& ctx)
{
    eng::Renderer& r = ctx.renderer;
    mSwordModel.setVisible(r, mWeapon == WSword);
    mStaffModel.setVisible(r, mWeapon == WStaff);
    mTorchModel.setVisible(r, mWeapon == WTorch);
}

} // namespace game
