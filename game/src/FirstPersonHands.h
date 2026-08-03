#pragma once

#include "PlayerWeapons.h"
#include "ViewmodelMotion.h"

#include <eng/Handles.h>
#include <eng/animation/SkeletalAnimation.h>

#include <memory>
#include <optional>
#include <string>

namespace eng { class Renderer; }

namespace game {

// One reusable first-person rig shared by every weapon. Weapon definitions only
// select authored clips; they never own, duplicate, or retarget hand skeletons.
//
// Two layers meet here and stay separable:
//   * skeletal animation (authored clips, stepped with the viewmodel channel)
//   * procedural motion  (ViewmodelMotion: socket, bob, sway, recoil, landing)
// The node transform is the procedural layer's output; the skin pose is the
// animation layer's. A future model/sprite presentation swaps one without
// touching the other.
class FirstPersonHands {
public:
    bool init(eng::Renderer& renderer, eng::NodeHandle headNode);
    void setWeapon(const WeaponViewmodelDef& definition, bool playDraw);
    bool triggerFire(eng::Renderer& renderer);
    // `animationDt` drives the authored clips (stepped, stop-motion channel);
    // `motionDt` drives the procedural layers. They differ on purpose: a
    // stepped clip beside a smoothly-placed rig is the intended look, and
    // stepping the sway reads as input lag centimetres from the eye.
    void update(eng::Renderer& renderer, float animationDt, float motionDt,
                const ViewmodelMotionInput& motion);
    std::optional<glm::vec3>
    muzzleWorldPosition(const eng::Renderer& renderer) const;
    // World matrix of the joint the muzzle hangs off (rig node * joint pose).
    // Tools that place the muzzle offset by hand need the frame it is
    // expressed in, not just the point it resolves to.
    std::optional<glm::mat4>
    muzzleJointWorld(const eng::Renderer& renderer) const;
    bool valid() const { return mSkin.valid() && mAnimator.valid(); }

    // Live tuning surface. The panel edits the tuning in place and the next
    // update() applies it; nothing needs a rebuild or a respawn.
    ViewmodelRig& rig() { return mMotion.tuning(); }
    const ViewmodelRig& rig() const { return mMotion.tuning(); }
    void setRig(const ViewmodelRig& tuning) { mMotion.setTuning(tuning); }
    const ViewmodelMotion& motion() const { return mMotion; }
    // Re-reads the weapon's feel numbers after the panel edited them.
    void refreshFeel(const WeaponViewmodelDef& definition)
    {
        mMotion.setFeel(viewmodelFeel(definition));
    }
    // Pose the rig without a live weapon or a simulation running: the editor
    // and the panel's freeze mode place the hands from tuning alone.
    void applyPose(eng::Renderer& renderer);

private:
    // Composes the procedural layers and writes the result to the rig node.
    void applyMotion(eng::Renderer& renderer, const ViewmodelMotionInput& in);

    std::shared_ptr<eng::animation::AnimationRig> mRig;
    eng::animation::SkeletalAnimator mAnimator;
    eng::SkinnedMeshHandle mMesh{};
    eng::SkinInstanceHandle mSkin{};
    eng::NodeHandle mNode{};
    ViewmodelMotion mMotion;
    std::string mIdleClip = "relax";
    std::string mFireClip = "grab.R";
    int mMuzzleJoint = -1;
    glm::vec3 mMuzzleOffset{0.0f, 0.025f, 0.0f};
};

} // namespace game
