#include "PlayerSystem.h"

#include "GameContext.h"

#include <eng/LightDesc.h>
#include <eng/Physics.h>
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
    mPlayer.sprintFovKick() = mTuning.sprintFovKick;
    // Sensitivity and FOV belong to whichever camera shape is running: the
    // figure that aims a crosshair spins a boom far too fast, and a
    // third-person lens is wider because the character is in frame.
    if (mMode == CameraMode::FirstPerson) {
        mPlayer.sensitivity() = mTuning.mouseSensitivity;
        mPlayer.setBaseFov(mTuning.baseFovDegrees);
    }
    mPlayer.bobAmount() = mTuning.bobAmount;
    mPlayer.bobSpeed() = mTuning.bobSpeed;
}

void PlayerSystem::setCameraTuning(const eng::ecs::ThirdPersonCamera& tuning)
{
    mCamera = tuning;
    mThirdPerson.setTuning(tuning);
    if (mMode == CameraMode::ThirdPerson) {
        mPlayer.sensitivity() = tuning.mouseSensitivity;
        mPlayer.setBaseFov(tuning.fovDegrees);
        mPlayer.turnRateDegrees() = tuning.turnRateDegrees;
    }
}

void PlayerSystem::setCameraMode(GameContext& ctx, CameraMode mode)
{
    if (mode == mMode)
        return;
    mMode = mode;
    // Dropping the lock on the way out of third person: the camera that framed
    // it no longer exists, and a first-person view that silently keeps facing
    // an enemy is a fight the player is no longer steering.
    if (mMode == CameraMode::FirstPerson)
        mLockOn.clear();
    applyCameraMode(ctx);
    // The eye node is new, so everything hanging off it -- the carried light,
    // the hands -- has to be re-seated.
    attachLoadout(ctx);
}

void PlayerSystem::applyCameraMode(GameContext& ctx)
{
    if (mMode == CameraMode::ThirdPerson) {
        mThirdPerson.setTuning(mCamera);
        mThirdPerson.setPhysics(&ctx.physics);
        mPlayer.setCameraRig(ctx.renderer, &mThirdPerson);
        mPlayer.setFacing(eng::FpsController::Facing::Movement);
        mPlayer.turnRateDegrees() = mCamera.turnRateDegrees;
        mPlayer.sensitivity() = mCamera.mouseSensitivity;
        mPlayer.setBaseFov(mCamera.fovDegrees);
    } else {
        mPlayer.setCameraRig(ctx.renderer, nullptr);
        mPlayer.setFacing(eng::FpsController::Facing::View);
        mPlayer.sensitivity() = mTuning.mouseSensitivity;
        mPlayer.setBaseFov(mTuning.baseFovDegrees);
    }
    rebuildAvatar(ctx);
    // The hands are a first-person device. In third person they would hang in
    // the air three metres in front of the camera.
    mHands.setVisible(ctx.renderer, mMode == CameraMode::FirstPerson);
}

void PlayerSystem::rebuildAvatar(GameContext& ctx)
{
    if (mAvatar.valid())
        eng::destroyPrimitive(ctx.renderer, ctx.physics, mAvatar);
    mAvatar = {};
    if (mMode != CameraMode::ThirdPerson)
        return;
    // A capsule the size of the character controller, parented to the node that
    // carries the body's facing -- so what you see turn is what the simulation
    // turned. Placeholder until there is a player model; the socket it hangs
    // on does not change when there is.
    eng::PrimitiveDesc desc;
    desc.mesh.kind = eng::PrimitiveKind::Capsule;
    desc.mesh.radius = 0.30f;
    desc.mesh.height = 1.70f;
    desc.parent = mPlayer.bodyNode();
    desc.position = glm::vec3(0.0f, 0.85f, 0.0f);
    desc.castShadows = true;
    mAvatar = eng::spawnPrimitive(ctx.renderer, ctx.physics, desc);
}

void PlayerSystem::applyLockOn(GameContext& ctx)
{
    (void)ctx;
    mThirdPerson.setLockOn(mLockOn.camera());
    glm::vec3 point{0.0f};
    // Facing the target is what turns strafing into circling. Only in third
    // person: in first person the body already faces wherever you look, and
    // overriding that would take the view off the player.
    if (mMode == CameraMode::ThirdPerson && mLockOn.targetPoint(point))
        mPlayer.setFacingTarget(point);
    else
        mPlayer.setFacingTarget(std::nullopt);
}

glm::vec3 PlayerSystem::aimOrigin() const
{
    return mPlayer.eyePosition();
}

glm::vec3 PlayerSystem::aimDirection() const
{
    glm::vec3 point{0.0f};
    if (mMode == CameraMode::ThirdPerson && mLockOn.targetPoint(point)) {
        const glm::vec3 delta = point - mPlayer.eyePosition();
        if (glm::length(delta) > 1e-3f)
            return glm::normalize(delta);
    }
    return mPlayer.forward();
}

void PlayerSystem::spawnAt(GameContext& ctx, glm::vec3 pos)
{
    // A level transition cleared the scene, so the rig's nodes are already
    // gone. Say so before init rebuilds them, or attach/detach would work on
    // handles the renderer has forgotten.
    mPlayer.cameraRig().forgetNodes();
    mAvatar = {};
    mPlayer.init(ctx.renderer, ctx.physics, pos, mTuning.moveSpeed,
                 mTuning.mouseSensitivity, glm::vec3(-1000.0f),
                 glm::vec3(1000.0f));
    mPlayer.setCeilingHeight(3.0f);
    // init() rebuilds the controller from scratch, so the lens tuning has to
    // be pushed again -- otherwise every level transition silently reset FOV
    // and bob to the engine defaults.
    setControllerTuning(mTuning);
    // ...and the camera shape with it: init() attached whichever rig was
    // installed, and the mode owns more than the rig (facing, sensitivity, the
    // avatar, whether the hands are drawn).
    applyCameraMode(ctx);
    mLockOn.clear();
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
    // init() built a fresh, visible rig; the camera shape decides whether it
    // stays that way.
    mHands.setVisible(r, mMode == CameraMode::FirstPerson);
}

void PlayerSystem::look(GameContext& ctx)
{
    mLastLookDelta = ctx.input.mouseDelta();
    mPlayer.applyLook(eng::FpsController::readCommand(ctx.input));
}

void PlayerSystem::present(GameContext& ctx, float alpha, float frameDt)
{
    mPlayer.present(ctx.renderer, alpha, frameDt);
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
