#include "ActorVisual.h"

#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace game::actor {

float ActorVisual::anchorRise() const
{
    return mAnchor == ActorAnchor::Centre ? mHeight * 0.5f : 0.0f;
}

bool ActorVisual::create(eng::Renderer& renderer, const ActorRig& rig,
                         eng::NodeHandle parent, const ActorVisualDesc& desc)
{
    destroy(renderer);
    if (!rig.valid() || !mAnimator.bind(rig))
        return false;

    mHeight = std::isfinite(desc.height) && desc.height > 0.01f ? desc.height
                                                                : rig.def().height;
    mAnchor = desc.anchor;

    // The rig is modelled at a known height; an actor states its own. One
    // uniform scale reconciles them, so a 1.4 m goblin and a 2.4 m brute are
    // the same mesh and the same clips -- and the drawn silhouette still
    // matches the capsule the gameplay side built from the same number.
    const float authored = std::max(0.01f, rig.def().height);
    mScale = mHeight / authored;

    mNode = renderer.createNode(parent);
    if (!mNode.valid())
        return false;
    // Height fit and art multiplier compose here, on the body's own node. The
    // parent keeps whatever scale it had, which matters because a parent's
    // scale would also multiply the offset below.
    glm::vec3 scale(mScale);
    if (std::isfinite(desc.scale.x) && std::isfinite(desc.scale.y) &&
        std::isfinite(desc.scale.z))
        scale *= desc.scale;
    renderer.setScale(mNode, scale);
    // Parented bodies never move again: the parent carries them. So the anchor
    // correction is baked in once, here, rather than re-applied every frame by
    // a setTransform the caller is not going to call.
    if (parent.id != eng::kRootNode.id)
        renderer.setPosition(mNode, glm::vec3(0.0f, -anchorRise(), 0.0f));

    mSkin = renderer.attachSkinnedMesh(mNode, rig.mesh(), desc.material,
                                       desc.castShadows, false);
    if (!mSkin.valid()) {
        renderer.destroyNode(mNode);
        mNode = {};
        return false;
    }
    renderer.setSkinningPose(mSkin, mAnimator.modelMatrices());
    return true;
}

void ActorVisual::destroy(eng::Renderer& renderer)
{
    if (mNode.valid())
        renderer.destroyNode(mNode);
    mNode = {};
    mSkin = {};
    mScale = 1.0f;
}

void ActorVisual::setTransform(eng::Renderer& renderer, glm::vec3 position,
                               float yawRadians)
{
    if (!mNode.valid())
        return;
    // The rig's origin is between its feet. Whatever frame the caller works in,
    // the conversion happens once, here.
    renderer.setPosition(mNode, feetOf(position));
    renderer.setOrientation(
        mNode, glm::angleAxis(yawRadians, glm::vec3(0.0f, 1.0f, 0.0f)));
}

void ActorVisual::update(eng::Renderer& renderer, float dt,
                         const ActorAnimationInput& input)
{
    if (!valid())
        return;
    mAnimator.update(dt, input);
    renderer.setSkinningPose(mSkin, mAnimator.modelMatrices());
}

void ActorVisual::setVisible(eng::Renderer& renderer, bool show)
{
    if (mNode.valid())
        renderer.setNodeVisible(mNode, show);
}

} // namespace game::actor
