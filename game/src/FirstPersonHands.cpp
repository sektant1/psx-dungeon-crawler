#include "FirstPersonHands.h"

#include <eng/Log.h>
#include <eng/Renderer.h>
#include <eng/assets/AssetRoot.h>

#include <glm/gtc/quaternion.hpp>

#include <filesystem>

namespace game {

bool FirstPersonHands::init(eng::Renderer& renderer,
                            eng::NodeHandle headNode)
{
    mRig.reset();
    mMesh = {};
    mSkin = {};
    mNode = {};
    mMuzzleJoint = -1;

    const std::filesystem::path skeleton = eng::assets::resolve(
        "animations/viewmodels/arms/arms_rig.skeleton.ozz");
    const std::filesystem::path model =
        eng::assets::resolve("meshes/viewmodels/arms_rig.glb");
    if (skeleton.empty() || model.empty()) {
        eng::log::warn(
            "First-person hands: cooked rig assets are unavailable");
        return false;
    }

    std::string error;
    mRig = eng::animation::AnimationRig::load(
        skeleton.string(), skeleton.parent_path().string(), &error);
    if (!mRig)
        return false;
    if (!mAnimator.setRig(mRig)) {
        eng::log::error("First-person hands: animator rejected cooked rig");
        return false;
    }
    mMesh = renderer.loadSkinnedMesh(model.string(), mRig->jointNames());
    if (!mMesh.valid())
        return false;

    const auto cleanup = [&] {
        if (mNode.valid())
            renderer.destroyNode(mNode);
        mNode = {};
        mSkin = {};
        if (mMesh.valid())
            renderer.releaseSkinnedMesh(mMesh);
        mMesh = {};
    };

    // Rig was authored around a standing Blender character: source up is about
    // 1.4 m and source depth sits around zero. The camera-space reframing, the
    // half turn onto -z and the arm-span scale are all authored data now
    // (ViewmodelRig), so the node is created at the origin and takes its
    // whole transform from applyPose below.
    mNode = renderer.createNode(headNode, glm::vec3(0.0f),
                                "first-person-hands");
    if (!mNode.valid()) {
        cleanup();
        return false;
    }
    mMotion.reset();
    applyPose(renderer);
    mSkin = renderer.attachSkinnedMesh(mNode, mMesh, "Game/FirstPersonHands",
                                       false, true);
    if (!mSkin.valid()) {
        cleanup();
        return false;
    }

    eng::animation::AnimationPlayOptions idle;
    idle.fadeSeconds = 0.0f;
    idle.loop = true;
    if (!mAnimator.play(mIdleClip, idle) || !mAnimator.update(0.0f)) {
        cleanup();
        return false;
    }
    if (!renderer.setSkinningPose(mSkin, mAnimator.modelMatrices())) {
        cleanup();
        return false;
    }
    return true;
}

void FirstPersonHands::setWeapon(const WeaponViewmodelDef& definition,
                                 bool playDraw)
{
    // The feel numbers are presentation, not animation: they apply even when
    // the cooked rig is missing and the skinned hands never came up.
    mMotion.setFeel(viewmodelFeel(definition));
    if (!mAnimator.valid())
        return;
    mIdleClip = mRig->hasClip(definition.handsIdleAnimation)
                    ? definition.handsIdleAnimation
                    : "relax";
    mFireClip = mRig->hasClip(definition.handsFireAnimation)
                    ? definition.handsFireAnimation
                    : "grab.R";
    mMuzzleJoint = mRig->jointIndex(definition.handsMuzzleJoint);
    mMuzzleOffset = definition.handsMuzzleOffset;
    if (mMuzzleJoint < 0)
        eng::log::warn("First-person hands: missing muzzle joint '%s'",
                       definition.handsMuzzleJoint.c_str());
    if (playDraw && mRig->hasClip(definition.handsDrawAnimation)) {
        eng::animation::AnimationPlayOptions draw;
        draw.fadeSeconds = 0.06f;
        draw.loop = false;
        draw.returnClip = mIdleClip;
        draw.returnFadeSeconds = 0.10f;
        mAnimator.play(definition.handsDrawAnimation, draw);
        return;
    }
    eng::animation::AnimationPlayOptions idle;
    idle.fadeSeconds = 0.10f;
    idle.loop = true;
    mAnimator.play(mIdleClip, idle);
}

bool FirstPersonHands::triggerFire(eng::Renderer& renderer)
{
    mMotion.kick();
    if (!valid())
        return false;
    eng::animation::AnimationPlayOptions fire;
    fire.fadeSeconds = 0.025f;
    fire.loop = false;
    fire.returnClip = mIdleClip;
    fire.returnFadeSeconds = 0.075f;
    if (!mAnimator.play(mFireClip, fire)) {
        eng::log::warn("First-person hands: missing fire clip '%s'",
                       mFireClip.c_str());
        return false;
    }
    // Sample immediately so projectile origin and submitted skin pose describe
    // same firing frame, even when several fixed steps precede presentation.
    return mAnimator.update(0.0f) &&
           renderer.setSkinningPose(mSkin, mAnimator.modelMatrices());
}

void FirstPersonHands::update(eng::Renderer& renderer, float animationDt,
                              float motionDt, const ViewmodelMotionInput& in)
{
    ViewmodelMotionInput motion = in;
    motion.dt = motionDt;
    // Procedural placement runs even without the cooked rig: it is what a
    // sprite or model presentation would ride on, and a missing skeleton must
    // not take the whole viewmodel offline.
    applyMotion(renderer, motion);
    if (!valid())
        return;
    if (mAnimator.update(animationDt))
        renderer.setSkinningPose(mSkin, mAnimator.modelMatrices());
}

void FirstPersonHands::applyMotion(eng::Renderer& renderer,
                                   const ViewmodelMotionInput& motion)
{
    if (!mNode.valid())
        return;
    const ViewmodelPose pose = mMotion.update(motion);
    renderer.setPosition(mNode, pose.position);
    renderer.setOrientation(mNode,
                            glm::quat(glm::radians(pose.rotationDegrees)));
    renderer.setScale(mNode, glm::vec3(pose.scale));
}

void FirstPersonHands::applyPose(eng::Renderer& renderer)
{
    // Zero-delta update: the socket and the current accumulator state, with no
    // time passing. Used at init and by the panel while motion is frozen.
    applyMotion(renderer, {});
}

std::optional<glm::mat4>
FirstPersonHands::muzzleJointWorld(const eng::Renderer& renderer) const
{
    const std::span<const glm::mat4> joints = mAnimator.modelMatrices();
    if (!valid() || mMuzzleJoint < 0 ||
        std::size_t(mMuzzleJoint) >= joints.size())
        return std::nullopt;

    eng::NodeTransform world;
    if (!renderer.nodeWorldTransform(mNode, world))
        return std::nullopt;
    const glm::mat4 node = glm::translate(glm::mat4(1.0f), world.position) *
                           glm::mat4_cast(world.orientation) *
                           glm::scale(glm::mat4(1.0f), world.scale);
    return node * joints[std::size_t(mMuzzleJoint)];
}

std::optional<glm::vec3>
FirstPersonHands::muzzleWorldPosition(const eng::Renderer& renderer) const
{
    const std::span<const glm::mat4> joints = mAnimator.modelMatrices();
    if (!valid() || mMuzzleJoint < 0 ||
        std::size_t(mMuzzleJoint) >= joints.size())
        return std::nullopt;

    eng::NodeTransform world;
    if (!renderer.nodeWorldTransform(mNode, world))
        return std::nullopt;
    const glm::vec3 modelPosition = glm::vec3(
        joints[std::size_t(mMuzzleJoint)] * glm::vec4(mMuzzleOffset, 1.0f));
    return world.position + world.orientation * (world.scale * modelPosition);
}

} // namespace game
