#pragma once

#include <string>

namespace game {

// "Show me the hands, holding this weapon, here."
//
// The first-person rig had no representation in the editor at all: a camera
// carrying a ViewmodelRig drew a cross and a line back to the eye, and the
// numbers behind it -- where a weapon sits in the hand, whether it clips the
// fingers, whether it is even the right size -- were only ever visible by
// launching the game. This component is the entity that was missing.
//
// It is authoring scaffolding, not level data: SceneCook drops it, and no
// runtime component corresponds to it. Which weapon an author was looking at
// while placing a camera is not something the map should carry.
//
// Authored on the same camera entity as FirstPersonController and ViewmodelRig,
// because in first person the camera *is* the head.
struct ViewmodelPreview {
    // A weapons.toml id. Empty shows the first slot, which is what the player
    // starts the level holding and therefore the right default to judge.
    std::string weapon;
    bool visible = true;
};

} // namespace game
