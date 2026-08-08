#include "FirstPersonHands.h"

#include <eng/Log.h>
#include <eng/Renderer.h>
#include <eng/assets/AssetRoot.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <filesystem>

namespace game {

bool FirstPersonHands::init(eng::Renderer& renderer, eng::NodeHandle headNode)
{
    return init(renderer, headNode, mHands);
}

bool FirstPersonHands::init(eng::Renderer& renderer, eng::NodeHandle headNode,
                            const HandsDefinition& hands)
{
    shutdown(renderer);
    mHands = hands;

    // A rig that states its own framing replaces the global placement.
    //
    // [player_viewmodel] is one offset/rotation/scale for every rig, and it
    // cannot be: it yaws 180 degrees because the SHIPPED rig faces glTF +z,
    // and the imported animation rigs already face -z. Applying it to those
    // turned them twice. Only the three placement fields are overridden -- bob,
    // sway, recoil and the rest stay global, because those are feel and feel
    // should not change with the hands.
    if (mHands.hasFraming) {
        ViewmodelRig framed = mMotion.tuning();
        framed.offset = mHands.framingOffset;
        framed.rotation = mHands.framingRotationDegrees;
        framed.scale = mHands.framingScale;
        mMotion.setTuning(framed);
    }

    const std::filesystem::path skeleton = eng::assets::resolve(mHands.skeleton);
    const std::filesystem::path model = eng::assets::resolve(mHands.model);
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

    const auto cleanup = [&] { shutdown(renderer); };

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
    // The arms get their own child at identity rather than hanging off mNode
    // directly, so a sprite weapon can hide them without hiding the sprite
    // stack that replaces them -- both live under mNode, and node visibility is
    // the only granularity the renderer offers. Identity transform, so the skin
    // pose, the sockets and the muzzle maths are all unchanged by it.
    mSkinNode = renderer.createNode(mNode, glm::vec3(0.0f), "hands-skin");
    if (!mSkinNode.valid()) {
        cleanup();
        return false;
    }
    mSkin = renderer.attachSkinnedMesh(mSkinNode, mMesh, mHands.material, false,
                                       true);
    if (!mSkin.valid()) {
        cleanup();
        return false;
    }

    mIdleClip = mHands.idleAnimation;
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

    // The sockets come up with the rig, not with the first weapon: the rig's
    // vocabulary is a property of the hands, and the editor's picker offers it
    // before any weapon is chosen.
    mSockets.build(renderer, mNode, *mRig, mHands.sockets);
    applySockets(renderer);
    return true;
}

void FirstPersonHands::shutdown(eng::Renderer& renderer)
{
    mWeapon.clear(renderer);
    mSprite.clear(renderer);
    mSockets.clear(renderer);
    if (mNode.valid())
        renderer.destroyNode(mNode);
    mNode = {};
    // Destroyed with mNode above, which owns it; only the handle is dropped.
    mSkinNode = {};
    mSkin = {};
    if (mMesh.valid())
        renderer.releaseSkinnedMesh(mMesh);
    mMesh = {};
    mRig.reset();
    mMuzzleJoint = -1;
    mMuzzleSocket = {};
    mWeaponSocket.clear();
}

void FirstPersonHands::bindMuzzle(const WeaponViewmodelDef& definition)
{
    mMuzzleSocket = {};
    mMuzzleOffset = definition.handsMuzzleOffset;
    mMuzzleJoint = -1;
    if (!mRig)
        return;

    // A named socket wins: it is authored once in viewmodel_hands.toml and
    // reused, where hands_muzzle_joint makes every weapon repeat a Blender
    // joint name. The older key stays as the fallback so nothing that ships
    // today has to be rewritten.
    if (!definition.muzzleSocket.empty()) {
        const auto& sockets = mHands.sockets;
        const auto found = std::find_if(
            sockets.begin(), sockets.end(), [&](const ViewmodelSocketDef& s) {
                return s.name == definition.muzzleSocket;
            });
        if (found != sockets.end()) {
            mMuzzleSocket = *found;
            mMuzzleJoint = mRig->jointIndex(found->joint);
            if (mMuzzleJoint >= 0)
                return;
        }
        eng::log::warn("First-person hands: no muzzle socket '%s' on this rig",
                       definition.muzzleSocket.c_str());
        mMuzzleSocket = {};
    }

    mMuzzleJoint = mRig->jointIndex(definition.handsMuzzleJoint);
    if (mMuzzleJoint < 0)
        eng::log::warn("First-person hands: missing muzzle joint '%s'",
                       definition.handsMuzzleJoint.c_str());
}

void FirstPersonHands::setWeapon(eng::Renderer& renderer,
                                 const WeaponViewmodelDef& definition,
                                 bool playDraw,
                                 const std::optional<eng::EnchantmentDesc>& glow)
{
    // The feel numbers are presentation, not animation: they apply even when
    // the cooked rig is missing and the skinned hands never came up.
    mMotion.setFeel(viewmodelFeel(definition));
    mWeaponSocket = definition.socket;
    mSpriteMuzzle = definition.spriteMuzzle;

    // Exactly one presentation is live. Both hang off mNode and therefore ride
    // the same procedural motion; what differs is whether the thing in frame is
    // a skinned rig holding geometry, or a stack of flat layers.
    if (definition.presentation == ViewmodelPresentation::Sprite) {
        mWeapon.clear(renderer);
        // The shared hand layers first, then the weapon's over them. Order does
        // not decide draw order -- SpriteViewmodel sorts by distance -- but it
        // does decide which of two identically-placed layers wins the sort's
        // stability, and a weapon leaning on the hands is the intent.
        std::vector<ViewmodelSpriteLayer> layers = mHands.spriteLayers;
        layers.insert(layers.end(), definition.spriteLayers.begin(),
                      definition.spriteLayers.end());
        mSprite.build(renderer, mNode, layers);
        // Hide the skinned arms rather than tearing them down: switching back
        // to a model weapon must not reload a skeleton, which is the one thing
        // in this rig that costs real time.
        if (mSkinNode.valid())
            renderer.setNodeVisible(mSkinNode, false);
        if (!mSprite.valid())
            eng::log::warn("First-person hands: sprite weapon has no drawable "
                           "layers");
    } else {
        mSprite.clear(renderer);
        if (mSkinNode.valid())
            renderer.setNodeVisible(mSkinNode, true);
        if (mHands.bundledWeapon) {
            // The rig IS the weapon. These are the imported FPS animation
            // packs, authored as hands already holding a gun -- which is why
            // their reloads work at all. Attaching the weapon's own model on
            // top would put a second gun in the same fist, and it is the rig's
            // clips, not a socket, that make the thing move.
            mWeapon.clear(renderer);
        } else {
            // Rebuild the held visual even when the skeleton did not come up:
            // the socket set is empty then, build() sees an invalid node and
            // leaves the weapon empty, which is the honest outcome rather than
            // a crash.
            mWeapon.build(renderer, mSockets.node(definition.socket),
                          definition, glow);
            if (!definition.socket.empty() && !mWeapon.valid() &&
                !mSockets.empty())
                eng::log::warn("First-person hands: no socket '%s' on this rig",
                               definition.socket.c_str());
        }
    }
    if (!mAnimator.valid())
        return;
    mIdleClip = mRig->hasClip(definition.handsIdleAnimation)
                    ? definition.handsIdleAnimation
                    : mHands.idleAnimation;
    mFireClip = mRig->hasClip(definition.handsFireAnimation)
                    ? definition.handsFireAnimation
                    : "grab.R";
    bindMuzzle(definition);
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

void FirstPersonHands::refreshAttachment(eng::Renderer& renderer,
                                         const WeaponViewmodelDef& definition)
{
    mWeapon.applyAttachment(renderer, definition);
}

bool FirstPersonHands::triggerFire(eng::Renderer& renderer)
{
    mMotion.kick();
    // Before the valid() gate: a sprite weapon must animate whether or not the
    // cooked skeleton came up. Its frames are the whole of its fire animation,
    // and they do not depend on the rig at all.
    if (mSprite.valid()) {
        mSprite.triggerFire();
        mSprite.update(renderer, 0.0f); // seat frame 0 of the run this instant
        return true;
    }
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
    // Sample immediately so projectile origin, held weapon and submitted skin
    // pose describe the same firing frame, even when several fixed steps
    // precede presentation.
    if (!mAnimator.update(0.0f) ||
        !renderer.setSkinningPose(mSkin, mAnimator.modelMatrices()))
        return false;
    applySockets(renderer);
    return true;
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
    // Sprite frames step on the same stopped-motion channel the authored clips
    // use, for the same reason: the presentation snaps, the placement does not.
    if (mSprite.valid())
        mSprite.update(renderer, animationDt);
    if (!valid())
        return;
    if (mAnimator.update(animationDt)) {
        renderer.setSkinningPose(mSkin, mAnimator.modelMatrices());
        // After the pose, never before: a socket posed from last frame's
        // skeleton puts the weapon one frame behind the hand holding it, which
        // reads as the weapon swimming in the grip during fast animations.
        applySockets(renderer);
    }
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

void FirstPersonHands::applySockets(eng::Renderer& renderer)
{
    if (!mAnimator.valid())
        return;
    mSockets.update(renderer, mAnimator.modelMatrices());
}

void FirstPersonHands::applyPose(eng::Renderer& renderer)
{
    // Zero-delta update: the socket and the current accumulator state, with no
    // time passing. Used at init and by the panel while motion is frozen.
    applyMotion(renderer, {});
    applySockets(renderer);
}

void FirstPersonHands::setVisible(eng::Renderer& renderer, bool show)
{
    if (mNode.valid())
        renderer.setNodeVisible(mNode, show);
}

std::optional<glm::mat4> FirstPersonHands::muzzleLocal() const
{
    const std::span<const glm::mat4> joints = mAnimator.modelMatrices();
    if (!valid() || mMuzzleJoint < 0 ||
        std::size_t(mMuzzleJoint) >= joints.size())
        return std::nullopt;
    // The socket is identity for a weapon that named a raw joint, so this is
    // the same arithmetic that path always did -- one code path, not two.
    return socketLocalMatrix(joints[std::size_t(mMuzzleJoint)], mMuzzleSocket);
}

std::optional<glm::mat4>
FirstPersonHands::muzzleJointWorld(const eng::Renderer& renderer) const
{
    const std::optional<glm::mat4> local = muzzleLocal();
    if (!local)
        return std::nullopt;

    eng::NodeTransform world;
    if (!renderer.nodeWorldTransform(mNode, world))
        return std::nullopt;
    const glm::mat4 node = glm::translate(glm::mat4(1.0f), world.position) *
                           glm::mat4_cast(world.orientation) *
                           glm::scale(glm::mat4(1.0f), world.scale);
    return node * *local;
}

std::optional<glm::vec3>
FirstPersonHands::muzzleWorldPosition(const eng::Renderer& renderer) const
{
    // A sprite weapon has no skeleton, so its muzzle is an authored point in
    // camera space rather than a joint. It still rides the rig node, so bob,
    // sway and recoil move the muzzle exactly as they do for a model weapon --
    // and aim is still the camera ray, so `aim != muzzle` holds unchanged.
    if (mSprite.valid()) {
        eng::NodeTransform spriteWorld;
        if (!renderer.nodeWorldTransform(mNode, spriteWorld))
            return std::nullopt;
        return spriteWorld.position +
               spriteWorld.orientation * (spriteWorld.scale * mSpriteMuzzle);
    }

    const std::optional<glm::mat4> local = muzzleLocal();
    if (!local)
        return std::nullopt;

    eng::NodeTransform world;
    if (!renderer.nodeWorldTransform(mNode, world))
        return std::nullopt;
    const glm::vec3 modelPosition =
        glm::vec3(*local * glm::vec4(mMuzzleOffset, 1.0f));
    return world.position + world.orientation * (world.scale * modelPosition);
}

std::optional<glm::mat4>
FirstPersonHands::weaponSocketWorld(const eng::Renderer& renderer) const
{
    eng::NodeTransform world;
    if (mWeaponSocket.empty() ||
        !mSockets.worldTransform(renderer, mWeaponSocket, world))
        return std::nullopt;
    return glm::translate(glm::mat4(1.0f), world.position) *
           glm::mat4_cast(world.orientation) *
           glm::scale(glm::mat4(1.0f), world.scale);
}

} // namespace game
