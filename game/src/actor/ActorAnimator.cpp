#include "ActorAnimator.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace game::actor {
namespace {

float clamp01(float value)
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

// Exponential approach, frame-rate independent. Used everywhere a weight or an
// angle chases a target: a lerp by `rate * dt` is the same curve only while the
// frame time is constant, and it visibly changes character between 30 and 144.
float approach(float current, float target, float rate, float dt)
{
    if (!(rate > 0.0f))
        return target;
    return target + (current - target) * std::exp(-rate * dt);
}

// How long each action holds the body, and whether the legs are allowed to keep
// running underneath it. An attack is an upper-body statement over locomotion;
// being staggered is not something you do while jogging.
struct ActionTraits {
    bool fullBody;
    // Where in the clip the blow lands, normalised. Fired once per play, for
    // gameplay that wants the pose rather than a timer.
    float strikePhase;
};

ActionTraits traitsOf(ActorAction action)
{
    switch (action) {
    case ActorAction::AttackLight:
    case ActorAction::AttackAlternate:
        return {false, 0.46f};
    case ActorAction::AttackHeavy:
        return {false, 0.55f};
    case ActorAction::Cast:
        return {false, 0.56f};
    case ActorAction::Hit:
        return {false, 0.30f};
    case ActorAction::Stagger:
    case ActorAction::Death:
        return {true, 0.20f};
    case ActorAction::None:
        break;
    }
    return {true, 0.5f};
}

} // namespace

bool ActorAnimator::bind(const ActorRig& rig)
{
    if (!rig.valid())
        return false;
    mRig = &rig;
    if (!mBlender.setRig(rig.rig())) {
        mRig = nullptr;
        return false;
    }
    mPhase = 0.0f;
    mIdleTime = 0.0f;
    mSpeed = 0.0f;
    mMoveBlend = 0.0f;
    mRunBlend = 0.0f;
    mDirection = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    mAirBlend = 0.0f;
    mAir = Air::Grounded;
    mAction = ActorAction::None;
    mActionWeight = 0.0f;
    mDead = false;
    return true;
}

void ActorAnimator::play(ActorAction action, float speed)
{
    if (action == ActorAction::None)
        return;
    // Death is terminal. Without this, a damage event that lands in the same
    // frame as the killing blow blends the corpse back up onto its feet.
    if (mDead)
        return;
    mAction = action;
    mActionTime = 0.0f;
    mActionSpeed = std::isfinite(speed) ? std::max(0.05f, speed) : 1.0f;
    mStruckThisAction = false;
    mActionStruck = false;
    mDead = action == ActorAction::Death;
}

void ActorAnimator::pushLayer(const std::string& clip, float phase01,
                              float weight, bool loop,
                              const eng::animation::JointMask* mask)
{
    if (!(weight > 1e-3f) || clip.empty() || !mRig->hasClip(clip))
        return;
    eng::animation::PoseLayer layer;
    layer.clip = clip;
    // Clips are sampled by PHASE, not by their own clock: every ground cycle
    // reads the same point in its own duration, so walk_f and walk_l stay on
    // the same footfall no matter that one is 1.00s and the other 0.92s.
    layer.time = phase01 * mRig->clipDuration(clip);
    layer.weight = weight;
    layer.loop = loop;
    layer.mask = mask;
    mLayers.push_back(std::move(layer));
}

void ActorAnimator::update(float dt, const ActorAnimationInput& input)
{
    if (!valid())
        return;
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.25f) : 0.0f;
    mLayers.clear();
    mOverlays.clear();
    mActionStruck = false;

    buildLocomotion(dt, input);
    buildAction(dt);
    buildLook(dt, input);

    mBlender.evaluate(mLayers, mOverlays);
}

void ActorAnimator::buildLocomotion(float dt, const ActorAnimationInput& input)
{
    const ActorRigDef& def = mRig->def();
    const ActorLocomotionTuning& tuning = def.locomotion;
    const ActorClipNames& clips = def.clips;

    // --- what the body is doing -------------------------------------------
    const glm::vec3 flat(input.velocity.x, 0.0f, input.velocity.z);
    const float speed = glm::length(flat);
    mSpeed = speed;

    // The node convention: forward is local -Z rotated by yaw. Same expression
    // as FpsController::forward() at zero pitch, deliberately -- if these two
    // ever disagree the avatar strafes when it should walk.
    const glm::vec3 forward(-std::sin(input.yawRadians), 0.0f,
                            -std::cos(input.yawRadians));
    // Right-handed, +Y up: forward x up points to the character's right.
    const glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const float along = speed > 1e-4f ? glm::dot(flat, forward) : 0.0f;
    const float across = speed > 1e-4f ? glm::dot(flat, right) : 0.0f;

    const float moveTarget =
        clamp01((speed - tuning.idleSpeed) /
                std::max(1e-3f, tuning.walkSpeed - tuning.idleSpeed));
    const float runTarget =
        clamp01((speed - tuning.walkSpeed) /
                std::max(1e-3f, tuning.runSpeed - tuning.walkSpeed));
    const float blendRate = 1.0f / std::max(1e-3f, tuning.postureBlend);
    mMoveBlend = approach(mMoveBlend, moveTarget, blendRate, dt);
    mRunBlend = approach(mRunBlend, runTarget, blendRate, dt);

    // --- the phase clock ---------------------------------------------------
    // Cadence comes from stride length, which is the in-place equivalent of
    // extracted root motion: the clip does not carry the character forward, so
    // its playback rate is what has to agree with the speed the simulation is
    // actually moving at. Get this wrong and the feet skate -- it is the single
    // most visible error in a locomotion system.
    const float stride =
        glm::mix(tuning.walkStride, tuning.runStride, mRunBlend);
    const float cadence = std::clamp(speed / std::max(0.05f, stride),
                                     tuning.minCadence, tuning.maxCadence);
    mPhase = std::fmod(mPhase + cadence * dt, 1.0f);
    if (mPhase < 0.0f)
        mPhase += 1.0f;

    // --- air ---------------------------------------------------------------
    const bool wasGrounded = mAir == Air::Grounded || mAir == Air::Landing;
    if (!input.grounded) {
        if (wasGrounded) {
            // Rising is a jump, already falling is a fall: the clips differ and
            // an actor knocked off a ledge should not appear to have chosen it.
            mAir = input.velocity.y > 0.5f ? Air::Jumping : Air::Falling;
            mAirTime = 0.0f;
        } else {
            mAirTime += dt;
            if (mAir == Air::Jumping &&
                mAirTime >= mRig->clipDuration(clips.jump))
                mAir = Air::Falling;
        }
    } else if (mAir == Air::Jumping || mAir == Air::Falling) {
        mAir = Air::Landing;
        mAirTime = 0.0f;
    } else if (mAir == Air::Landing) {
        mAirTime += dt;
        if (mAirTime >= mRig->clipDuration(clips.land))
            mAir = Air::Grounded;
    }

    const float airTarget = mAir == Air::Grounded ? 0.0f : 1.0f;
    mAirBlend = approach(mAirBlend, airTarget, blendRate, dt);
    const float groundWeight = 1.0f - mAirBlend;

    // --- standing ----------------------------------------------------------
    const std::string* stanceClip = &clips.idle;
    switch (mStance) {
    case ActorStance::Combat: stanceClip = &clips.idleCombat; break;
    case ActorStance::Dormant: stanceClip = &clips.dormant; break;
    case ActorStance::Talking: stanceClip = &clips.talk; break;
    case ActorStance::Relaxed: break;
    }
    // A stance idle has its own beat and must not be driven by the stride
    // clock, or a creeping enemy breathes in slow motion.
    if (groundWeight * (1.0f - mMoveBlend) > 1e-3f) {
        eng::animation::PoseLayer layer;
        layer.clip = *stanceClip;
        mIdleTime += dt;
        layer.time = mIdleTime;
        layer.weight = groundWeight * (1.0f - mMoveBlend);
        layer.loop = true;
        if (mRig->hasClip(layer.clip))
            mLayers.push_back(std::move(layer));
    }

    // --- moving ------------------------------------------------------------
    //
    // Two hemispheres, not one circle. Strafe cycles are authored with one leg
    // crossing in front of the other so they blend cleanly with the forward
    // run; blended against the BACKWARD run instead, that same crossing drives
    // one leg through the other. So the strafes pair with whichever of forward
    // and back the body is actually doing, and the two hemispheres cross over
    // through the near-pure-sideways pose where the pairing does not matter.
    if (groundWeight * mMoveBlend > 1e-3f) {
        const float total = std::abs(along) + std::abs(across);
        const float forwardShare = total > 1e-4f ? along / total : 1.0f;
        const float sideShare = total > 1e-4f ? std::abs(across) / total : 0.0f;

        const float back = clamp01(-forwardShare);
        const float ahead = clamp01(forwardShare);
        mDirection = glm::vec4(ahead, back, across < 0.0f ? sideShare : 0.0f,
                               across > 0.0f ? sideShare : 0.0f);

        const float moving = groundWeight * mMoveBlend;
        // Run has no authored strafe pair, so sideways motion keeps the walk
        // strafes and only the along-axis cycle escalates. A sprinting sidestep
        // reads as a fast shuffle, which is what it is.
        pushLayer(clips.walkForward, mPhase,
                  moving * ahead * (1.0f - mRunBlend), true, nullptr);
        pushLayer(clips.runForward, mPhase, moving * ahead * mRunBlend, true,
                  nullptr);
        pushLayer(clips.walkBack, mPhase, moving * back * (1.0f - mRunBlend),
                  true, nullptr);
        pushLayer(clips.runBack, mPhase, moving * back * mRunBlend, true,
                  nullptr);
        pushLayer(clips.walkLeft, mPhase, moving * mDirection.z, true, nullptr);
        pushLayer(clips.walkRight, mPhase, moving * mDirection.w, true, nullptr);
    }

    // --- air clips ---------------------------------------------------------
    if (mAirBlend > 1e-3f) {
        switch (mAir) {
        case Air::Jumping:
            pushLayer(clips.jump,
                      clamp01(mAirTime /
                              std::max(0.01f, mRig->clipDuration(clips.jump))),
                      mAirBlend, false, nullptr);
            break;
        case Air::Falling: {
            const float duration = std::max(0.01f, mRig->clipDuration(clips.fall));
            pushLayer(clips.fall, std::fmod(mAirTime, duration) / duration,
                      mAirBlend, true, nullptr);
            break;
        }
        case Air::Landing:
            pushLayer(clips.land,
                      clamp01(mAirTime /
                              std::max(0.01f, mRig->clipDuration(clips.land))),
                      mAirBlend, false, nullptr);
            break;
        case Air::Grounded:
            break;
        }
    }
}

void ActorAnimator::buildAction(float dt)
{
    const ActorActionTuning& tuning = mRig->def().action;
    if (mAction == ActorAction::None) {
        mActionWeight = approach(mActionWeight, 0.0f,
                                 1.0f / std::max(1e-3f, tuning.blendOut), dt);
        return;
    }

    const ActionTraits traits = traitsOf(mAction);
    const std::string& clip = mRig->clipFor(mAction);
    const float duration = std::max(0.01f, mRig->clipDuration(clip));
    mActionTime += dt * mActionSpeed;
    const float phase = clamp01(mActionTime / duration);

    if (!mStruckThisAction && phase >= traits.strikePhase) {
        mStruckThisAction = true;
        mActionStruck = true;
    }

    const bool quick = mAction == ActorAction::Hit;
    const float fadeIn = quick ? tuning.hitBlendIn : tuning.blendIn;
    const float fadeOut = quick ? tuning.hitBlendOut : tuning.blendOut;

    // Weight ramps in, holds, and ramps out against the END of the clip -- not
    // against a separate timer, so retiming the clip cannot leave the blend
    // hanging past the pose it was blending.
    float target = 1.0f;
    const float remaining = (1.0f - phase) * duration;
    if (remaining < fadeOut && !mDead)
        target = clamp01(remaining / std::max(1e-3f, fadeOut));
    mActionWeight = approach(mActionWeight, target,
                             1.0f / std::max(1e-3f, fadeIn), dt);

    if (phase >= 1.0f && !mDead) {
        // Held until the fade has actually finished, or the last frames of a
        // swing pop back to the run.
        if (mActionWeight < 0.02f)
            mAction = ActorAction::None;
    }

    if (mActionWeight <= 1e-3f)
        return;

    // Upper body always; lower body only to the extent the actor is not
    // locomoting. Two DISJOINT masks, so the halves add up rather than
    // double-counting the way two overlapping ones would.
    const float held = mDead ? 1.0f : mActionWeight;
    const float lower =
        traits.fullBody ? held : held * (1.0f - mMoveBlend * (1.0f - mAirBlend));
    pushLayer(clip, phase, held, false, &mRig->upperBody());
    pushLayer(clip, phase, lower, false, &mRig->lowerBody());
}

void ActorAnimator::buildLook(float dt, const ActorAnimationInput& input)
{
    const ActorLookTuning& tuning = mRig->def().look;
    const std::vector<std::pair<int, float>>& chain = mRig->lookChain();
    if (chain.empty())
        return;

    float yawTarget = 0.0f;
    float pitchTarget = 0.0f;
    // A corpse does not track you.
    if (input.lookTarget && !mDead) {
        const glm::vec3 delta = *input.lookTarget - input.eyePosition;
        const float flat = std::sqrt(delta.x * delta.x + delta.z * delta.z);
        if (flat > 1e-3f) {
            const float targetYaw = std::atan2(delta.x, delta.z);
            float relative = targetYaw - input.yawRadians;
            while (relative > glm::pi<float>())
                relative -= glm::two_pi<float>();
            while (relative < -glm::pi<float>())
                relative += glm::two_pi<float>();
            const float maxYaw = glm::radians(tuning.maxYawDegrees);
            const float maxPitch = glm::radians(tuning.maxPitchDegrees);
            // Past the yaw limit the actor gives up rather than straining at
            // it: a head pinned at its clamp reads as a bug, a head facing
            // forward reads as "has not noticed you".
            if (std::abs(relative) <= maxYaw * 1.35f) {
                yawTarget = std::clamp(relative, -maxYaw, maxYaw);
                pitchTarget = std::clamp(std::atan2(delta.y, flat), -maxPitch,
                                         maxPitch);
            }
        }
    }

    mLookYaw = approach(mLookYaw, yawTarget, tuning.responsiveness, dt);
    mLookPitch = approach(mLookPitch, pitchTarget, tuning.responsiveness, dt);
    if (std::abs(mLookYaw) < 1e-4f && std::abs(mLookPitch) < 1e-4f)
        return;

    for (const auto& [joint, share] : chain) {
        eng::animation::JointOverlay overlay;
        overlay.joint = joint;
        // Character space: yaw about up, then pitch about the character's
        // right. Spread down the chain so the chest leads and the head
        // finishes, which is how a real turn propagates.
        const glm::quat yaw =
            glm::angleAxis(mLookYaw * share, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat pitch =
            glm::angleAxis(-mLookPitch * share, glm::vec3(-1.0f, 0.0f, 0.0f));
        overlay.rotation = glm::normalize(yaw * pitch);
        mOverlays.push_back(overlay);
    }
}

} // namespace game::actor
