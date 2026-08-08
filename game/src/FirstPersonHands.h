#pragma once

#include "HandsDefinition.h"
#include "PlayerWeapons.h"
#include "SpriteViewmodel.h"
#include "ViewmodelMotion.h"
#include "ViewmodelSocket.h"
#include "WeaponViewmodel.h"

#include <eng/Handles.h>
#include <eng/animation/SkeletalAnimation.h>
#include <eng/render/Enchantment.h>

#include <memory>
#include <optional>
#include <string>

namespace eng { class Renderer; }

namespace game {

// One reusable first-person rig shared by every weapon. Weapon definitions only
// select authored clips and a socket; they never own, duplicate, or retarget
// hand skeletons.
//
// Three layers meet here and stay separable:
//   * skeletal animation (authored clips, stepped with the viewmodel channel)
//   * procedural motion  (ViewmodelMotion: socket, bob, sway, recoil, landing)
//   * attachment         (ViewmodelSocketSet + WeaponViewmodel: what is held)
// The node transform is the procedural layer's output; the skin pose is the
// animation layer's; the weapon rides a socket node driven by the skin pose. A
// future model/sprite presentation swaps one without touching the others.
class FirstPersonHands {
public:
    // `hands` is the authored rig (assets/config/viewmodel_hands.toml). The
    // default overload keeps the shipped arms, so a caller that has no opinion
    // -- a test, the editor before a project is open -- still gets hands.
    bool init(eng::Renderer& renderer, eng::NodeHandle headNode);
    bool init(eng::Renderer& renderer, eng::NodeHandle headNode,
              const HandsDefinition& hands);
    void shutdown(eng::Renderer& renderer);

    // Equips a weapon: clips, feel, muzzle binding, and the visual that hangs
    // off the named socket. Takes the renderer because equipping is when the
    // held geometry is built -- the old signature could not, which is the
    // mechanical reason weapons rendered as empty hands.
    void setWeapon(eng::Renderer& renderer, const WeaponViewmodelDef& definition,
                   bool playDraw,
                   const std::optional<eng::EnchantmentDesc>& glow = std::nullopt);
    bool triggerFire(eng::Renderer& renderer);
    // `animationDt` drives the authored clips (stepped, stop-motion channel);
    // `motionDt` drives the procedural layers. They differ on purpose: a
    // stepped clip beside a smoothly-placed rig is the intended look, and
    // stepping the sway reads as input lag centimetres from the eye.
    void update(eng::Renderer& renderer, float animationDt, float motionDt,
                const ViewmodelMotionInput& motion);
    std::optional<glm::vec3>
    muzzleWorldPosition(const eng::Renderer& renderer) const;
    // World matrix of the frame the muzzle is expressed in (rig node * joint
    // pose * socket). Tools that place the muzzle offset by hand need the frame
    // it lives in, not just the point it resolves to.
    std::optional<glm::mat4>
    muzzleJointWorld(const eng::Renderer& renderer) const;
    // Likewise for the socket the weapon hangs on, which is what the weapon
    // attach gizmo anchors to.
    std::optional<glm::mat4>
    weaponSocketWorld(const eng::Renderer& renderer) const;
    bool valid() const { return mSkin.valid() && mAnimator.valid(); }

    // Live tuning surface. The panel edits the tuning in place and the next
    // update() applies it; nothing needs a rebuild or a respawn.
    ViewmodelRig& rig() { return mMotion.tuning(); }
    const ViewmodelRig& rig() const { return mMotion.tuning(); }
    void setRig(const ViewmodelRig& tuning) { mMotion.setTuning(tuning); }
    // Copy the live placement back into the rig's own framing.
    //
    // The Viewmodel panel edits the live ViewmodelRig, which is where a rig's
    // framing was applied at init -- so without this, dialling in a rig and then
    // switching weapons discards the work: the next init() re-applies the
    // framing the file still holds. Called by the panel after an edit, so the
    // number in hand survives a rig swap and can be read back out for the TOML.
    void captureFraming()
    {
        if (!mHands.hasFraming)
            return;
        const ViewmodelRig& live = mMotion.tuning();
        mHands.framingOffset = live.offset;
        mHands.framingRotationDegrees = live.rotation;
        mHands.framingScale = live.scale;
    }
    // What the rig's framing currently is, for the panel to print as TOML.
    const HandsDefinition& definitionRef() const { return mHands; }
    const ViewmodelMotion& motion() const { return mMotion; }
    // Re-reads the weapon's feel numbers after the panel edited them.
    void refreshFeel(const WeaponViewmodelDef& definition)
    {
        mMotion.setFeel(viewmodelFeel(definition));
    }
    // Re-seats the held weapon after the gizmo or the panel moved its attach
    // offset. Cheap: it writes one transform and does not reload the mesh.
    void refreshAttachment(eng::Renderer& renderer,
                           const WeaponViewmodelDef& definition);
    // Pose the rig without a live weapon or a simulation running: the editor
    // and the panel's freeze mode place the hands from tuning alone.
    void applyPose(eng::Renderer& renderer);
    void setVisible(eng::Renderer& renderer, bool show);

    // The rig's socket vocabulary, for the editor's picker and the panel's
    // readout: an author chooses from what this rig actually has.
    const ViewmodelSocketSet& sockets() const { return mSockets; }
    const HandsDefinition& definition() const { return mHands; }
    const WeaponViewmodel& weapon() const { return mWeapon; }
    // The sprite stack, live only while a `presentation = "sprite"` weapon is
    // equipped. Empty otherwise, which is how a caller asks which presentation
    // is on screen without the rig growing a mode enum of its own.
    const SpriteViewmodel& sprite() const { return mSprite; }
    eng::NodeHandle node() const { return mNode; }

private:
    // Composes the procedural layers and writes the result to the rig node.
    void applyMotion(eng::Renderer& renderer, const ViewmodelMotionInput& in);
    // Re-poses the socket nodes from this frame's skeleton pose.
    void applySockets(eng::Renderer& renderer);
    // Resolves this weapon's muzzle to a joint plus a socket-space transform.
    void bindMuzzle(const WeaponViewmodelDef& definition);
    std::optional<glm::mat4> muzzleLocal() const;

    HandsDefinition mHands = defaultHandsDefinition();
    std::shared_ptr<eng::animation::AnimationRig> mRig;
    eng::animation::SkeletalAnimator mAnimator;
    eng::SkinnedMeshHandle mMesh{};
    eng::SkinInstanceHandle mSkin{};
    eng::NodeHandle mNode{};
    // The arms, on their own child of mNode at identity, so a sprite weapon can
    // hide them without hiding the sprite stack that stands in for them.
    eng::NodeHandle mSkinNode{};
    ViewmodelMotion mMotion;
    ViewmodelSocketSet mSockets;
    WeaponViewmodel mWeapon;
    // The two presentations are mutually exclusive and both hang off mNode, so
    // they inherit the same procedural motion. Exactly one is non-empty at a
    // time; setWeapon is the only place that decides which.
    SpriteViewmodel mSprite;
    // Camera-space muzzle for the sprite presentation, which has no skeleton to
    // hang one on. Only meaningful while mSprite is live.
    glm::vec3 mSpriteMuzzle{0.0f};
    std::string mWeaponSocket;
    std::string mIdleClip = "relax";
    std::string mFireClip = "grab.R";
    // The muzzle as a socket rather than a special case: joint index, the
    // socket transform on it (identity when the weapon names a raw joint), and
    // the weapon's own nudge on top.
    int mMuzzleJoint = -1;
    ViewmodelSocketDef mMuzzleSocket;
    glm::vec3 mMuzzleOffset{0.0f, 0.025f, 0.0f};
};

} // namespace game
