#include "PlayerSystem.h"

#include "GameContext.h"

#include <eng/LightDesc.h>
#include <eng/Renderer.h>

#include <cmath>
#include <string>

namespace game {

void PlayerSystem::spawnAt(GameContext& ctx, glm::vec3 pos)
{
    mPlayer.init(ctx.renderer, ctx.physics, pos, mSpeed, mSens,
                 glm::vec3(-1000.0f), glm::vec3(1000.0f));
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
    mSwordModel.init(r, mPlayer.headNode(), assets + "/meshes/props");
    mStaffModel.initStaff(r, mPlayer.headNode(),
                          assets + "/meshes/crystal_spire1.obj");
    mTorchModel.initTorch(r, mPlayer.headNode());
    applyWeaponVis(ctx);
}

void PlayerSystem::update(GameContext& ctx, float dt)
{
    mPlayer.update(ctx.input, ctx.renderer, dt);
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
