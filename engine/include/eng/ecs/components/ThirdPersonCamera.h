#pragma once

namespace eng::ecs {

// An over-the-shoulder orbit camera with a lock-on, authored on the camera
// entity that frames the player -- the third-person counterpart of
// FirstPersonController, and the component that decides which of the two shapes
// a level plays in.
//
// Same contract as its first-person sibling: this is *tuning*, not a second
// camera implementation. eng::ThirdPersonCameraRig is the runtime object; a
// scene carrying this component says "play me over the shoulder, with these
// numbers", and a scene carrying neither runs on the game's config defaults.
//
// The defaults are souls-shaped rather than shooter-shaped: a short boom, a
// low-ish pivot at chest height, a slow vertical follow so stairs do not pump
// the camera, and a lock that frames a point between the player and the target
// instead of staring at the target alone.
struct ThirdPersonCamera {
    // Metres the eye sits behind the pivot with nothing in the way. The single
    // number that decides how the game reads: short is intimate and claustro-
    // phobic, long turns a duel into a diagram.
    float distance = 3.4f;
    // Height of the pivot above the character's feet. Chest, not eye: framing
    // the chest keeps the head off the top of the screen when the camera looks
    // down, which is where a lock-on spends most of its time.
    float pivotHeight = 1.45f;
    // Lateral offset of the pivot, +right. The over-the-shoulder bias: it moves
    // the character off centre so the space they are walking into is the space
    // the player can see.
    float shoulderOffset = 0.42f;
    // Exponential follow rates, per second. Horizontal is fast (the camera
    // should not feel towed); vertical is deliberately much slower, because
    // vertical is where stairs, steps and landings live and a camera that
    // tracks them exactly is the classic third-person nausea.
    float followRate = 18.0f;
    float followRateVertical = 7.0f;
    // Orbit pitch range, degrees. Negative looks down (the eye rises), positive
    // looks up (the eye drops). Asymmetric on purpose: a lot of downward room
    // for reading the ground around you, much less upward, because an eye that
    // drops far ends up inside the floor and fighting the spring arm.
    float pitchMinDegrees = -62.0f;
    float pitchMaxDegrees = 32.0f;
    // Radians of orbit per pixel of mouse movement. Separate from the
    // first-person figure: the same sensitivity that aims a crosshair spins a
    // boom far too fast.
    float mouseSensitivity = 0.0026f;
    // Spring arm. The boom shortens instantly when something comes between the
    // pivot and the eye -- a camera that eases *into* a wall clips through it --
    // and extends back out at pushOutSpeed once the way is clear, which is the
    // asymmetry that keeps a doorway from strobing.
    float collisionRadius = 0.25f;
    float pushOutSpeed = 5.0f; // metres per second
    float minDistance = 0.6f;  // never closer than this, even when pinched
    // How fast the body turns to face where it is going, degrees per second.
    // High: a souls character pivots, it does not arc.
    float turnRateDegrees = 780.0f;
    // The lens. Wider than the first-person value because the character is in
    // frame and needs room around them.
    float fovDegrees = 66.0f;

    // --- lock-on ---------------------------------------------------------
    // 0 frames the player, 1 frames the target. A third of the way is the
    // souls answer: both readable, the target slightly above centre, and the
    // player's own space still visible at the bottom of the screen.
    float lockFramingBias = 0.34f;
    // How fast the camera swings onto (and off) a target, per second. Fast
    // enough to feel like a snap, slow enough not to be a cut.
    float lockBlendRate = 9.0f;
    // Extra downward tilt while locked, degrees. Puts the target above the
    // centre line and the ground the player is about to roll across below it.
    float lockPitchDegrees = -6.0f;
    // The boom lengthens with the distance to the target, up to this much, so
    // a fight at range does not push both fighters into the same few pixels.
    float lockDistanceBoost = 1.1f; // metres at full range

    // An authored camera kept in the scene but not applied -- same idea as
    // Camera::active and FirstPersonController::active.
    bool active = true;
};

} // namespace eng::ecs
