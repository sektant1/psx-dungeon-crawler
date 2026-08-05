#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <cmath>

namespace eng {
class Physics;
class Renderer;

// What the character tells the camera, once per rendered frame.
//
// This type is the seam the camera system is built on: a *controller* simulates
// a body, a *rig* decides where the view sits. Before it, both lived inside
// FpsController::present(), so a second camera shape could only have arrived as
// an `if` in the middle of it -- and the third-person case needs a different
// node chain, a different pitch range and a wall query, none of which fit
// behind a flag.
//
// Everything here is already interpolated between fixed steps by the caller. A
// rig never reads a physics body, never runs at the simulation rate, and never
// writes back into the simulation: what a camera does cannot move a capsule.
struct CameraPose {
    // The character's feet, world space, interpolated between fixed steps.
    glm::vec3 focus{0.0f};
    // Head offset in body space: eye height plus whatever bob/crouch/dash arc
    // the controller simulated. First person rides it directly; third person
    // takes only its height, because a bob applied to a boom is nausea.
    glm::vec3 headOffset{0.0f, 1.7f, 0.0f};
    // Where the player is looking. The authoritative angles live on the
    // controller so that aiming, interaction and the camera agree.
    float viewYaw = 0.0f;
    float viewPitch = 0.0f;
    // Where the body points. Equal to viewYaw in first person; in third person
    // the body turns towards travel (or towards a lock-on target) while the
    // camera orbits independently.
    float facingYaw = 0.0f;
    // Presentation-only roll, radians -- the dodge roll, today.
    float rollRadians = 0.0f;
    // Horizontal speed over the controller's base speed, and the signed
    // fraction of it that is lateral (+1 = full right strafe). Feel layers
    // (lean, FOV, boom lag) read these rather than re-deriving velocity.
    float speedRatio = 0.0f;
    float strafeRatio = 0.0f;
    bool grounded = true;
    // Downward speed at the frame the character touched down, m/s, and zero on
    // every other frame. The caller clears it once it has been handed over, so
    // a rig that ignores it cannot accumulate one.
    float landingImpact = 0.0f;
};

// Camera-space shake, applied at presentation only. Deliberately not part of
// the pose: the pose is what the simulation believes, and a shake must never
// desync collision or send a shot somewhere the player did not point.
struct CameraShake {
    glm::vec3 offset{0.0f};
    glm::vec3 rotationDegrees{0.0f};
};

// Where a lock-on camera points. Gameplay fills this in every frame; the engine
// never learns what a target *is*, only that there is a point worth framing.
// That is the whole boundary -- enemy selection, line of sight and the rules
// for dropping a lock are the game's, and the rig is the framing.
struct CameraLockOn {
    bool active = false;
    glm::vec3 point{0.0f};
    // How big the thing is, for framing: a boss wants to be pushed further down
    // the screen than a rat does.
    float radius = 0.6f;
};

// Frame-rate-independent exponential smoothing: the fraction of the remaining
// distance covered in dt is 1 - e^(-rate*dt), so the same `rate` produces the
// same motion at 30 and at 240 fps.
//
// The naive lerp(current, target, k) every frame does not: k is per *frame*, so
// a camera tuned on a 60 Hz machine is twice as loose on a 30 Hz one and half
// as loose on a 144 Hz one. Every easing in this file goes through here.
inline float smoothTowards(float current, float target, float rate, float dt)
{
    if (rate <= 0.0f || dt <= 0.0f)
        return rate <= 0.0f ? target : current;
    const float t = 1.0f - std::exp(-rate * dt);
    return current + (target - current) * t;
}

inline glm::vec3 smoothTowards(glm::vec3 current, glm::vec3 target, float rate,
                               float dt)
{
    return {smoothTowards(current.x, target.x, rate, dt),
            smoothTowards(current.y, target.y, rate, dt),
            smoothTowards(current.z, target.z, rate, dt)};
}

// The equivalent angle in (-pi, pi]. Angles have to be differenced this way or
// a camera swinging past north takes the long way round the compass.
inline float wrapAngle(float radians)
{
    constexpr float kTwoPi = 6.283185307179586f;
    radians = std::fmod(radians + 3.141592653589793f, kTwoPi);
    if (radians < 0.0f)
        radians += kTwoPi;
    return radians - 3.141592653589793f;
}

inline float smoothAngleTowards(float current, float target, float rate,
                                float dt)
{
    return current + smoothTowards(0.0f, wrapAngle(target - current), rate, dt);
}

// Rotate towards `target` by at most maxDelta radians, the short way. Used for
// a body turning to face where it is going: a turn has a *rate*, not an easing
// curve -- a character that eases into its facing looks like it is skating.
inline float turnTowards(float current, float target, float maxDelta)
{
    const float delta = wrapAngle(target - current);
    if (std::abs(delta) <= maxDelta)
        return wrapAngle(target);
    return wrapAngle(current + (delta > 0.0f ? maxDelta : -maxDelta));
}

// The yaw that looks along a world direction, in the renderer's convention:
// yaw 0 looks down -Z, and forward(yaw) is (-sin yaw, 0, -cos yaw).
inline float yawOfDirection(glm::vec3 direction)
{
    return std::atan2(-direction.x, -direction.z);
}

// A camera shape. Owns its own node chain and takes the renderer's camera when
// it is attached, so swapping first person for third person is swapping one of
// these -- not editing the controller that drives it.
class CameraRig {
public:
    virtual ~CameraRig() = default;

    // Build the node chain and take the camera. Called again after a scene
    // clear, which destroys every node this rig owned.
    virtual void attach(Renderer& renderer) = 0;
    // Drop the node chain. Safe to call on a rig that was never attached.
    virtual void detach(Renderer& renderer) = 0;
    // The scene was cleared out from under the rig, so every node it owned is
    // already gone. Forget them without touching the renderer: destroying a
    // stale handle is the one way this goes wrong, and a level transition does
    // it on every load.
    virtual void forgetNodes() = 0;

    // Once per rendered frame, with the real frame delta -- not the fixed step.
    // A camera keeps easing while the world is frozen, and quantising its
    // smoothing to the simulation rate is visible as stepping.
    virtual void present(Renderer& renderer, const CameraPose& pose,
                         float dt) = 0;

    // The node the camera hangs off: where a first-person viewmodel and a
    // carried light attach.
    virtual NodeHandle eyeNode() const = 0;
    // The node that tracks the character itself, oriented by facingYaw. An
    // avatar mesh parents here, and in first person it is the body node the
    // eye already hangs off.
    virtual NodeHandle characterNode() const = 0;

    virtual glm::vec3 eyePosition() const = 0;
    virtual glm::vec3 forward() const = 0;

    // Does the view sit in the character's head? What decides whether the
    // viewmodel is drawn -- and the one question gameplay is allowed to ask a
    // rig, because the answer changes what it presents, not how it simulates.
    virtual bool firstPerson() const = 0;

    // The pitch range the *controller* clamps its authoritative angle to. The
    // rig owns the limits (a boom that can pass under the floor is a different
    // constraint from a neck), but the controller enforces them, so pushing the
    // mouse past the limit never builds up an angle the player has to unwind.
    virtual glm::vec2 pitchLimitsRadians() const
    {
        return {glm::radians(-89.0f), glm::radians(89.0f)};
    }

    // Gameplay's lock-on request. Ignored by rigs that do not frame a target.
    virtual void setLockOn(const CameraLockOn& lock) { (void)lock; }

    // True while the rig, not the mouse, owns the view angles -- lock-on. The
    // controller adopts them after present(), so movement stays camera-relative
    // and releasing the lock leaves the view exactly where the camera left it.
    virtual bool viewOverride(float& yaw, float& pitch) const
    {
        (void)yaw;
        (void)pitch;
        return false;
    }

    // The world to query for a spring arm. Null is fine: a rig that cannot ask
    // where the walls are simply does not push in.
    virtual void setPhysics(Physics* physics) { (void)physics; }

    void setShake(glm::vec3 offset, glm::vec3 rotationDegrees)
    {
        mShake.offset = offset;
        mShake.rotationDegrees = rotationDegrees;
    }
    const CameraShake& shake() const { return mShake; }

protected:
    CameraShake mShake;
};

} // namespace eng
