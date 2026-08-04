#pragma once

#include <glm/glm.hpp>

namespace game {

// Where the shared first-person rig sits relative to the eye, and how strongly
// it is allowed to move there.
//
// This is the half of viewmodel presentation that belongs to the *player*, not
// to any one weapon: swapping weapons must never move the hands out of frame,
// so the camera-space socket, the scale and the global motion multipliers are
// authored once and every weapon only leans out of them.
//
// It is authored in three places that are all the same numbers:
//   * game.toml's [player_viewmodel] -- the game's default framing
//   * a ViewmodelRig component on a scene's camera entity -- a level's override
//   * the debug console's Viewmodel panel (F1) -- live tuning, copied back out
//
// A plain struct with no dependencies on purpose: it is a serialisable
// component (see game/src/scene/ComponentRegistry.cpp), so it must not drag
// weapon, physics or renderer headers into the editor and the cooker.
struct ViewmodelRig {
    // Camera space: +x right, +y up, -z forward (the camera looks down -z).
    //
    // Close to the eye and only slightly low: the hands read as the player's
    // own rather than as a model held out in front of them, which is what the
    // 0.75 m stand-off looked like once the cooked rig replaced the placeholder
    // it was dialled against.
    glm::vec3 offset{0.0f, -0.880f, -0.055f};
    // The source rig faces glTF +z; the camera faces -z, hence the half turn.
    glm::vec3 rotation{0.0f, 180.0f, 0.0f}; // degrees: pitch, yaw, roll
    float scale = 0.50f;

    // Global multipliers over the per-weapon feel numbers. A designer dials the
    // whole presentation from here without editing three weapon definitions;
    // 0 switches a layer off entirely.
    float bobScale = 1.0f;
    float swayScale = 1.0f;
    float recoilScale = 1.0f;

    // Movement bob. Speed is normalised against `bobReferenceSpeed` so the bob
    // reads the same whether the player's tuned move speed is 3 or 8 m/s.
    float bobReferenceSpeed = 6.0f;
    float bobRollDegrees = 1.4f;

    // Mouse-look sway: the hands lag the view, then are pulled back to centre.
    float swayReturn = 9.0f;    // 1/s toward centre
    float swayMax = 0.05f;      // metres; clamps a fast flick
    float swayRollDegrees = 3.0f;

    // Landing impulse: a dip on touchdown, recovered like the recoil spring.
    float landingDip = 0.055f;
    float landingRecovery = 9.0f;

    // Freeze every procedural layer. Authoring a static pose is impossible
    // while the rig is breathing, so the panel needs one switch for it.
    bool motionEnabled = true;
};

// Every field finite, scale positive, multipliers non-negative. A rejected
// table leaves the caller's tuning untouched rather than half-applied.
bool validViewmodelRig(const ViewmodelRig& rig);

} // namespace game
