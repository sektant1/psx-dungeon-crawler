#include "PlayerSystem.h"

#include "GameContext.h"

#include <eng/LightDesc.h>
#include <eng/Input.h>
#include <eng/Renderer.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace game {

void PlayerSystem::setControllerTuning(
    const eng::ecs::FirstPersonController& tuning)
{
    mTuning = tuning;
    // The live controller too, not just the next spawn: a level transition is
    // not the only time this changes -- the debug panel and an authored scene
    // both expect the view to answer immediately.
    mPlayer.speed() = mTuning.moveSpeed;
    mPlayer.sensitivity() = mTuning.mouseSensitivity;
    mPlayer.setBaseFov(mTuning.baseFovDegrees);
    mPlayer.sprintFovKick() = mTuning.sprintFovKick;
    mPlayer.bobAmount() = mTuning.bobAmount;
    mPlayer.bobSpeed() = mTuning.bobSpeed;
}

void PlayerSystem::spawnAt(GameContext& ctx, glm::vec3 pos)
{
    mPlayer.init(ctx.renderer, ctx.physics, pos, mTuning.moveSpeed,
                 mTuning.mouseSensitivity, glm::vec3(-1000.0f),
                 glm::vec3(1000.0f));
    mPlayer.setCeilingHeight(3.0f);
    // init() rebuilds the controller from scratch, so the lens tuning has to
    // be pushed again -- otherwise every level transition silently reset FOV
    // and bob to the engine defaults.
    setControllerTuning(mTuning);
}

bool PlayerSystem::loadWeapons(const std::string& definitionsPath)
{
    const bool loaded = mWeaponLibrary.load(definitionsPath);
    mWeapons.bind(&mWeaponLibrary.defs());
    return loaded;
}

bool PlayerSystem::loadHands(const std::string& definitionsPath)
{
    return loadHandsDefinition(definitionsPath, mHandsDefinition);
}

void PlayerSystem::attachLoadout(GameContext& ctx)
{
    eng::Renderer& r = ctx.renderer;
    eng::LightDesc carry;
    carry.colour = glm::vec3(std::pow(1.0f, 2.2f), std::pow(0.76f, 2.2f),
                             std::pow(0.54f, 2.2f)) * 0.72f;
    carry.range = 6.0f;
    r.attachLight(mPlayer.headNode(), carry);
    mWeapons.resetRuntime();
    if (mHands.init(r, mPlayer.headNode(), mHandsDefinition) &&
        mWeapons.selected())
        mHands.setWeapon(r, mWeapons.selected()->viewmodel, false);
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
        if (mWeapons.selected())
            mHands.setWeapon(ctx.renderer, mWeapons.selected()->viewmodel, true);
    }
    if (fired)
        mHands.triggerFire(ctx.renderer);
    return fired;
}

void PlayerSystem::updateViewmodels(GameContext& ctx, float animationDt,
                                    float frameDt)
{
    ViewmodelMotionInput motion;
    motion.horizontalSpeed = mPlayer.horizontalSpeed();
    motion.grounded = mPlayer.grounded();
    motion.lookDelta = mLastLookDelta;
    mHands.update(ctx.renderer, animationDt, frameDt, motion);
    // Consumed: look() refills it every frame the player drives the camera, so
    // holding the last delta would sway the hands forever in a menu.
    mLastLookDelta = glm::vec2(0.0f);
}

void PlayerSystem::setViewmodelRig(const ViewmodelRig& tuning)
{
    mHands.setRig(tuning);
}

void PlayerSystem::refreshViewmodel(GameContext& ctx)
{
    if (const PlayerWeaponDef* weapon = mWeapons.selected()) {
        mHands.refreshFeel(weapon->viewmodel);
        mHands.refreshAttachment(ctx.renderer, weapon->viewmodel);
    }
    mHands.applyPose(ctx.renderer);
}

void PlayerSystem::rebuildWeaponViewmodel(GameContext& ctx)
{
    if (const PlayerWeaponDef* weapon = mWeapons.selected())
        mHands.setWeapon(ctx.renderer, weapon->viewmodel, false);
    mHands.applyPose(ctx.renderer);
}

std::optional<glm::vec3>
PlayerSystem::projectileMuzzle(const eng::Renderer& renderer) const
{
    return mHands.muzzleWorldPosition(renderer);
}

const PlayerWeaponDef* PlayerSystem::weaponDefinition(std::size_t index) const
{
    const auto& definitions = mWeaponLibrary.defs();
    return index < definitions.size() ? &definitions[index] : nullptr;
}

} // namespace game
