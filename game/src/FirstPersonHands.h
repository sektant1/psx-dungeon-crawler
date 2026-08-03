#pragma once

#include "PlayerWeapons.h"

#include <eng/Handles.h>
#include <eng/animation/SkeletalAnimation.h>

#include <memory>
#include <optional>
#include <string>

namespace eng { class Renderer; }

namespace game {

// One reusable first-person rig shared by every weapon. Weapon definitions only
// select authored clips; they never own, duplicate, or retarget hand skeletons.
class FirstPersonHands {
public:
    bool init(eng::Renderer& renderer, eng::NodeHandle headNode);
    void setWeapon(const WeaponViewmodelDef& definition, bool playDraw);
    bool triggerFire(eng::Renderer& renderer);
    void update(eng::Renderer& renderer, float dt);
    std::optional<glm::vec3>
    muzzleWorldPosition(const eng::Renderer& renderer) const;
    bool valid() const { return mSkin.valid() && mAnimator.valid(); }

private:
    std::shared_ptr<eng::animation::AnimationRig> mRig;
    eng::animation::SkeletalAnimator mAnimator;
    eng::SkinnedMeshHandle mMesh{};
    eng::SkinInstanceHandle mSkin{};
    eng::NodeHandle mNode{};
    std::string mIdleClip = "relax";
    std::string mFireClip = "grab.R";
    int mMuzzleJoint = -1;
    glm::vec3 mMuzzleOffset{0.0f, 0.025f, 0.0f};
};

} // namespace game
