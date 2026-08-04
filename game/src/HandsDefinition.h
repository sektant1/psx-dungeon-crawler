#pragma once

#include "ViewmodelSocket.h"

#include <string>
#include <vector>

namespace game {

// The player's first-person hands, authored once.
//
// Which mesh and skeleton the rig is, and -- the part weapons care about -- the
// named sockets it offers. Before this, all four were literals in
// FirstPersonHands::init, so replacing the hands meant editing C++ and the
// socket vocabulary did not exist at all.
//
// Kept free of renderer and weapon headers so the editor and a test can load it
// without a GPU: it is content, like enemies.toml or the kit catalogue.
struct HandsDefinition {
    // Logical asset paths, resolved through eng::assets like every other.
    std::string skeleton = "animations/viewmodels/arms/arms_rig.skeleton.ozz";
    std::string model = "meshes/viewmodels/arms_rig.glb";
    std::string material = "Game/FirstPersonHands";
    // Played when a weapon names no idle clip of its own, and the clip the rig
    // returns to after a draw or a shot.
    std::string idleAnimation = "relax";
    std::vector<ViewmodelSocketDef> sockets;
};

// The shipped rig: hand.R/hand.L plus the fingertip the finger-gun weapons
// fire from. Used when viewmodel_hands.toml is missing, so a fresh checkout
// still has hands rather than a warning and an empty screen.
HandsDefinition defaultHandsDefinition();

// Paths non-empty, every socket valid, no duplicate socket names.
bool validHandsDefinition(const HandsDefinition& hands);

// `[hands]` and its `[[hands.socket]]` array out of a TOML document. A missing
// file or a missing section leaves `out` untouched, which is how the caller's
// default survives; a malformed one returns false and leaves it untouched too,
// because a half-applied rig is worse than the shipped one.
bool loadHandsDefinition(const std::string& tomlPath, HandsDefinition& out);
bool parseHandsDefinition(const char* tomlSource, HandsDefinition& out);

} // namespace game
