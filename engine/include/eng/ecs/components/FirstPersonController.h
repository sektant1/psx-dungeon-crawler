#pragma once

namespace eng::ecs {

// The player's body and eye, authored on the camera entity that is that eye.
//
// eng::FpsController is the simulation -- capsule, ground check, sprint, slide,
// dash -- and stays a runtime object; this component is the *tuning* an author
// puts on a scene so a level can say how its player moves without a rebuild.
// Nothing here creates a second controller: a game reads the component off the
// camera it spawns the player at and pushes the numbers into the controller it
// already has (see game/src/PlayerSystem.h).
//
// Why the camera and not a bare spawn point: in first person the camera *is*
// the player's head, so "add a first-person controller to this camera" is the
// sentence an author actually means, and the same entity is then where the
// viewmodel rig hangs (game::ViewmodelRig). A scene with no such component runs
// on the game's own config defaults exactly as before.
struct FirstPersonController {
    // Metres per second at a full walk. Sprint and crouch scale off it.
    float moveSpeed = 3.0f;
    // Radians of view per pixel of mouse movement.
    float mouseSensitivity = 0.002f;
    // Vertical FOV the view sits at before any locomotion feedback.
    float baseFovDegrees = 70.0f;
    // Degrees added at full sprint. Restrained on purpose -- a camera that
    // zooms hard while running reads as motion sickness, not speed.
    float sprintFovKick = 4.0f;
    // Head bob: vertical amplitude in metres, and cycle speed. The camera's
    // bob, not the weapon's -- the viewmodel has its own, louder one.
    float bobAmount = 0.025f;
    float bobSpeed = 8.5f;
    // An authored controller kept in the scene but not applied. Same idea as
    // Camera::active: parking a tuning beats deleting and retyping it.
    bool active = true;
};

} // namespace eng::ecs
