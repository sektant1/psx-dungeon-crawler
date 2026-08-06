#pragma once

#include <eng/Handles.h>
#include <eng/animation/SkeletalAnimation.h>

#include <memory>
#include <string>
#include <vector>

namespace eng { class Renderer; }

namespace game::actor {

// The resting posture, which is a statement about the situation rather than
// about motion: the same walk plays under all of them.
enum class ActorStance { Relaxed, Combat, Dormant, Talking };

// A one-shot. Gameplay fires these as events; the animator owns how long they
// take and what they interrupt.
//
// Not the same vocabulary as game::ActorAction in audio/ActorSounds.h, which is
// a closed list of things that make a NOISE and is authored per creature in the
// cooked map. The two overlap but do not coincide -- an animation needs three
// attack variants and a stagger; a sound table needs Interact and Dodge -- and
// merging them would put rows in the editor's cue picker that play nothing.
enum class ActorAction {
    None,
    AttackLight,
    AttackAlternate,
    AttackHeavy,
    Cast,
    Hit,
    Stagger,
    Death,
};

// What an actor's body is, as data (assets/config/actors.toml).
//
// Kept free of renderer and ECS headers so a test, the editor and the cooker
// can read it without a GPU -- the same rule HandsDefinition follows, and for
// the same reason: this is content.
struct ActorClipNames {
    std::string idle = "idle";
    std::string idleCombat = "idle_combat";
    std::string dormant = "dormant";
    std::string talk = "talk";
    std::string walkForward = "walk_f";
    std::string walkBack = "walk_b";
    std::string walkLeft = "walk_l";
    std::string walkRight = "walk_r";
    std::string runForward = "run_f";
    std::string runBack = "run_b";
    std::string jump = "jump";
    std::string fall = "fall";
    std::string land = "land";
    std::string attackLight = "attack_1";
    std::string attackAlternate = "attack_2";
    std::string attackHeavy = "attack_heavy";
    std::string cast = "cast";
    std::string hit = "hit";
    std::string stagger = "stagger";
    std::string death = "death";
};

// How speed becomes cadence, and how postures cross over. Every number here is
// live-tunable from the debug panel; none of them is read on a hot path more
// than once per actor per frame.
struct ActorLocomotionTuning {
    float walkStride = 1.50f;
    float runStride = 3.40f;
    float idleSpeed = 0.15f;
    float walkSpeed = 1.60f;
    float runSpeed = 4.20f;
    float minCadence = 0.45f;
    float maxCadence = 2.40f;
    float postureBlend = 0.18f;
};

struct ActorActionTuning {
    float blendIn = 0.07f;
    float blendOut = 0.16f;
    float hitBlendIn = 0.03f;
    float hitBlendOut = 0.12f;
};

struct ActorLookTuning {
    std::vector<std::string> joints{"chest", "neck", "head"};
    std::vector<float> share{0.25f, 0.35f, 0.40f};
    float maxYawDegrees = 70.0f;
    float maxPitchDegrees = 35.0f;
    float responsiveness = 9.0f;
};

struct ActorRigDef {
    std::string skeleton = "animations/actors/humanoid/humanoid_rig.skeleton.ozz";
    std::string clipDirectory = "animations/actors/humanoid";
    std::string model = "meshes/actors/humanoid_rig.glb";
    std::string material = "Game/Actor/Default";
    float height = 1.8f;
    std::vector<std::string> upperBody{"spine"};
    ActorClipNames clips;
    ActorLocomotionTuning locomotion;
    ActorActionTuning action;
    ActorLookTuning look;
};

// `[actor]` out of a TOML document. A missing file or section leaves `out`
// untouched -- the built-in default is the shipped humanoid, so a checkout
// without the config still has bodies rather than capsules.
bool loadActorRigDef(const std::string& tomlPath, ActorRigDef& out);
bool parseActorRigDef(const char* tomlSource, ActorRigDef& out);

// The loaded rig: skeleton, clips, skinned geometry and the masks derived from
// them. ONE of these is shared by every actor in the world -- a hundred goblins
// are a hundred SkinInstances over one SkinnedMesh and one AnimationRig, which
// is the difference between this and a per-enemy animation object.
class ActorRig {
public:
    // Loads skeleton + clips + mesh. Returns false and logs on failure; the
    // caller falls back to primitive bodies rather than drawing nothing.
    bool load(eng::Renderer&, const ActorRigDef&);
    void unload(eng::Renderer&);
    bool valid() const { return mRig && mRig->valid() && mMesh.valid(); }

    const ActorRigDef& def() const { return mDef; }
    ActorRigDef& tuning() { return mDef; }
    // By value: the stored pointer is non-const and a reference to a
    // const-qualified one would bind to a temporary. Called when an actor is
    // built, not per frame.
    std::shared_ptr<const eng::animation::AnimationRig> rig() const
    {
        return mRig;
    }
    eng::SkinnedMeshHandle mesh() const { return mMesh; }
    const eng::animation::JointMask& upperBody() const { return mUpperBody; }
    const eng::animation::JointMask& lowerBody() const { return mLowerBody; }
    // Joint index + share for each link of the look chain, resolved once.
    const std::vector<std::pair<int, float>>& lookChain() const
    {
        return mLookChain;
    }
    // Clip durations, resolved once: the locomotion blend converts a shared
    // phase into a per-clip time every frame and must not hash a string to do
    // it.
    float clipDuration(const std::string& clip) const;
    bool hasClip(const std::string& clip) const;

    // Which clip plays a given one-shot, and how long it runs. Here rather than
    // inside the animator because the clip table is the rig's: gameplay that
    // wants to retime a swing to its own windup needs the authored length, and
    // reaching into an animator instance to ask a question about the asset is
    // the wrong direction.
    const std::string& clipFor(ActorAction) const;
    float clipDurationFor(ActorAction action) const
    {
        return clipDuration(clipFor(action));
    }
    // Playback rate that makes `action` last `seconds`. Clamped: a clip
    // stretched past about half or double its authored speed stops reading as
    // the motion it was drawn as, and a windup that extreme wants its own clip.
    float clipSpeedFor(ActorAction action, float seconds) const;

private:
    ActorRigDef mDef;
    std::shared_ptr<eng::animation::AnimationRig> mRig;
    eng::SkinnedMeshHandle mMesh{};
    eng::animation::JointMask mUpperBody;
    eng::animation::JointMask mLowerBody;
    std::vector<std::pair<int, float>> mLookChain;
};

} // namespace game::actor
