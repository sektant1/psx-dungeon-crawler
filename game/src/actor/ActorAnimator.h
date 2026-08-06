#pragma once

#include "ActorRig.h"

#include <eng/animation/SkeletalAnimation.h>

#include <glm/glm.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace game::actor {

// What the game knows about a body this frame. Everything the animator needs
// and nothing it does not: no entity, no renderer, no AI. The player's
// character controller, an enemy's brain and an NPC's idle all fill the same
// struct, which is why there is one animator rather than three.
struct ActorAnimationInput {
    glm::vec3 velocity{0.0f}; // world space, m/s
    // Where the body is facing, in the ENGINE'S NODE CONVENTION: forward is
    // local -Z, so a yaw of 0 faces -Z and forward is (-sin, 0, -cos). That is
    // what FpsController::forward() returns and what setOrientation on a node
    // means, so the player and an authored NPC rotation both arrive here
    // already correct.
    //
    // EnemySystem is the exception and converts at its call site: its own
    // `forwardOf` is (sin, 0, cos), the opposite, which never showed while
    // enemies were rotationally symmetric capsules.
    float yawRadians = 0.0f;
    bool grounded = true;
    // Where the actor is looking, if it is looking at anything. World space;
    // the animator turns it into a yaw/pitch relative to `yawRadians` and
    // spreads it down the look chain.
    std::optional<glm::vec3> lookTarget;
    glm::vec3 eyePosition{0.0f};
};

// One actor's animation state.
//
// Owns clocks and weights; owns no geometry. The composition it produces is:
//
//     locomotion  (idle/walk/run, 4-way, phase-locked)     -- full body
//   + posture     (jump/fall/land)                         -- full body
//   + action      (attack/cast/hit/death)  upper mask + lower mask
//   + look        (chest/neck/head)                        -- overlay
//
// Each is a separate method with its own numbers, so a change to how attacks
// blend cannot alter how a run reads.
class ActorAnimator {
public:
    // Binds to a shared rig. Cheap per actor: the skeleton, the clips and the
    // masks all live in the ActorRig, and this allocates only the scratch the
    // blender needs.
    bool bind(const ActorRig& rig);
    bool valid() const { return mRig && mBlender.valid(); }

    void setStance(ActorStance stance) { mStance = stance; }
    ActorStance stance() const { return mStance; }

    // Starts a one-shot. `speed` scales its playback, so a fast attack's
    // animation and its AttackDef windup can be made to agree without a second
    // clip. Ignored while dead -- death is terminal and must not be blended out
    // of by a late-arriving hit.
    void play(ActorAction action, float speed = 1.0f);
    ActorAction action() const { return mAction; }
    bool actionPlaying() const { return mAction != ActorAction::None; }
    // True on the frame the action's strike lands, for gameplay that wants to
    // hang a hitbox or an effect off the pose rather than off a timer.
    bool actionStruck() const { return mActionStruck; }

    void update(float dt, const ActorAnimationInput& input);

    // The finished pose, in skeleton order, for Renderer::setSkinningPose.
    std::span<const glm::mat4> modelMatrices() const
    {
        return mBlender.modelMatrices();
    }

    // --- readouts, for the debug panel ------------------------------------
    float speed() const { return mSpeed; }
    float locomotionPhase() const { return mPhase; }
    float runBlend() const { return mRunBlend; }
    float actionWeight() const { return mActionWeight; }
    std::span<const eng::animation::PoseLayer> layers() const { return mLayers; }

private:
    void buildLocomotion(float dt, const ActorAnimationInput& input);
    void buildAction(float dt);
    void buildLook(float dt, const ActorAnimationInput& input);

    void pushLayer(const std::string& clip, float phase01, float weight,
                   bool loop, const eng::animation::JointMask* mask);

    const ActorRig* mRig = nullptr;
    eng::animation::PoseBlender mBlender;
    std::vector<eng::animation::PoseLayer> mLayers;
    std::vector<eng::animation::JointOverlay> mOverlays;

    ActorStance mStance = ActorStance::Relaxed;

    // Locomotion. One phase for every ground cycle: walk_f, walk_l and run_f
    // are read at the same point in their own duration, so a diagonal blend
    // keeps both legs on the same beat instead of averaging two cycles that
    // disagree about which foot is down.
    float mPhase = 0.0f;
    // The stance idle runs on its own clock: a breathing cycle is not a
    // function of how fast the actor is walking.
    float mIdleTime = 0.0f;
    float mSpeed = 0.0f;
    float mMoveBlend = 0.0f; // 0 = standing, 1 = fully locomoting
    float mRunBlend = 0.0f;  // 0 = walk cycle, 1 = run cycle
    glm::vec4 mDirection{1.0f, 0.0f, 0.0f, 0.0f}; // forward, back, left, right

    // Air. `mAirBlend` is how much of the pose the jump/fall/land clips own.
    float mAirBlend = 0.0f;
    float mAirTime = 0.0f;
    enum class Air { Grounded, Jumping, Falling, Landing } mAir = Air::Grounded;

    // Action.
    ActorAction mAction = ActorAction::None;
    float mActionTime = 0.0f;
    float mActionSpeed = 1.0f;
    float mActionWeight = 0.0f;
    bool mActionStruck = false;
    bool mStruckThisAction = false;
    bool mDead = false;

    // Look, smoothed toward its target so an enemy acquiring you turns its head
    // rather than teleporting it.
    float mLookYaw = 0.0f;
    float mLookPitch = 0.0f;
};

} // namespace game::actor
