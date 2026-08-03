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
    // 1.4 m and source depth sits around zero. Reframe it into camera space,
    // then scale wide arm span to stay inside common horizontal FOVs.
    mNode = renderer.createNode(headNode, {0.0f, -0.95f, -0.75f},
                                "first-person-hands");
    if (!mNode.valid()) {
        cleanup();
        return false;
    }
    // Source rig faces glTF +Z. Camera and engine gameplay face -Z.
    renderer.setOrientation(
        mNode, glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)));
    renderer.setScale(mNode, glm::vec3(0.50f));
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

void FirstPersonHands::update(eng::Renderer& renderer, float dt)
{
    if (!valid())
        return;
    if (mAnimator.update(dt))
        renderer.setSkinningPose(mSkin, mAnimator.modelMatrices());
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
