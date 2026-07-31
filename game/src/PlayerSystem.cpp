#include "PlayerSystem.h"

#include "GameContext.h"

#include <eng/LightDesc.h>
#include <eng/Input.h>
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

bool PlayerSystem::loadWeapons(const std::string& definitionsPath)
{
    const bool loaded = mWeaponLibrary.load(definitionsPath);
    mWeapons.bind(&mWeaponLibrary.defs());
    return loaded;
}

void PlayerSystem::attachLoadout(GameContext& ctx)
{
    eng::Renderer& r = ctx.renderer;
    eng::LightDesc carry;
    carry.colour = glm::vec3(std::pow(1.0f, 2.2f), std::pow(0.76f, 2.2f),
                             std::pow(0.54f, 2.2f)) * 0.72f;
    carry.range = 6.0f;
    r.attachLight(mPlayer.headNode(), carry);
    mViewmodels.clear();
    mViewmodels.resize(mWeaponLibrary.defs().size());
    mPendingFireAnimation.assign(mViewmodels.size(), false);
    const CombatVocabulary& vocab = ctx.vocabulary;
    for (std::size_t i = 0; i < mViewmodels.size(); ++i) {
        const PlayerWeaponDef& weapon = mWeaponLibrary.defs()[i];
        mViewmodels[i].initPlayerWeapon(
            r, mPlayer.headNode(), weapon.viewmodel,
            {vocab.palette(weapon.viewmodel.glowSchool),
             weapon.viewmodel.glowStrength});
    }
    mWeapons.resetRuntime();
    setWeaponEnchant(r, mWeaponEnchant); // fresh viewmodels start from the flag
    applyWeaponVis(ctx);
}

void PlayerSystem::setWeaponEnchant(eng::Renderer& r, bool on)
{
    mWeaponEnchant = on;
    for (ViewModel& model : mViewmodels)
        model.setEnchantEnabled(r, on);
}

void PlayerSystem::look(GameContext& ctx)
{
    mLastLookDelta = ctx.input.mouseDelta();
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

void PlayerSystem::sampleWeaponInput(GameContext& ctx, bool enabled)
{
    const bool recapturedThisFrame = enabled && !mWeaponInputWasEnabled;
    mWeaponInputWasEnabled = enabled;
    WeaponCommand command;
    command.enabled = enabled;
    command.fireHeld = enabled && !recapturedThisFrame &&
                       ctx.input.isMouseDown(eng::MouseButton::Left);
    command.firePressed = enabled && !recapturedThisFrame &&
                          ctx.input.wasMouseClicked();
    command.swapPressed = enabled && ctx.input.wasPressed("swap_weapon");
    if (enabled && ctx.input.wasPressed("weapon_1")) command.selectSlot = 0;
    if (enabled && ctx.input.wasPressed("weapon_2")) command.selectSlot = 1;
    if (enabled && ctx.input.wasPressed("weapon_3")) command.selectSlot = 2;
    mWeapons.sample(command);
}

std::optional<std::size_t> PlayerSystem::fixedStepWeapons(
    GameContext& ctx, Mana& arc, bool canFire, float fixedDt)
{
    const std::optional<std::size_t> fired =
        mWeapons.fixedUpdate(fixedDt, arc, canFire);
    if (mWeapons.consumeSelectionChanged()) {
        applyWeaponVis(ctx);
        if (mWeapons.selectedIndex() < mViewmodels.size())
            mViewmodels[mWeapons.selectedIndex()].beginEquip();
    }
    if (fired && *fired < mPendingFireAnimation.size())
        mPendingFireAnimation[*fired] = true;
    return fired;
}

void PlayerSystem::updateViewmodels(GameContext& ctx, float dt)
{
    for (std::size_t i = 0; i < mViewmodels.size(); ++i) {
        mViewmodels[i].configure(mWeaponLibrary.defs()[i].viewmodel);
        mViewmodels[i].update(ctx.renderer, dt, mPendingFireAnimation[i],
                              mPlayer.horizontalSpeed(), mLastLookDelta,
                              mPlayer.grounded());
        mPendingFireAnimation[i] = false;
    }
}

const PlayerWeaponDef* PlayerSystem::weaponDefinition(std::size_t index) const
{
    const auto& definitions = mWeaponLibrary.defs();
    return index < definitions.size() ? &definitions[index] : nullptr;
}

void PlayerSystem::applyWeaponVis(GameContext& ctx)
{
    eng::Renderer& r = ctx.renderer;
    for (std::size_t i = 0; i < mViewmodels.size(); ++i)
        mViewmodels[i].setVisible(r, i == mWeapons.selectedIndex());
}

} // namespace game
