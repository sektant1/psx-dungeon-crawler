#pragma once

#include "SpriteViewmodel.h"
#include "ViewmodelSocket.h"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
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
    // Stable id and display label. Empty on the shipped default, which is the
    // rig that exists when there is no file to name one.
    std::string id;
    std::string displayName;
    // Logical asset paths, resolved through eng::assets like every other.
    std::string skeleton = "animations/viewmodels/arms/arms_rig.skeleton.ozz";
    std::string model = "meshes/viewmodels/arms_rig.glb";
    std::string material = "Game/FirstPersonHands";
    // Played when a weapon names no idle clip of its own, and the clip the rig
    // returns to after a draw or a shot.
    std::string idleAnimation = "relax";
    // The weapon is PART OF THIS RIG.
    //
    // The imported FPS animation packs were authored as hands already holding a
    // gun, which is why their reloads work: the left hand actually finds the
    // magazine well, and the bolt cycles because it is a bone. A weapon that
    // names such a rig must NOT then attach its own model on top -- that would
    // put a second gun in the same fist.
    //
    // False for the procedural rigs, which are bare hands and expect a weapon
    // on a socket. Both shapes the FPS brief asked for, in one field.
    bool bundledWeapon = false;

    // Per-rig framing, overriding [player_viewmodel] in game.toml.
    //
    // That block is ONE offset/rotation/scale, tuned against the shipped arms
    // rig. It cannot serve rigs authored by five different people: it applies a
    // 180-degree yaw because the shipped rig faces glTF +z, and the imported
    // rigs already face -z (tools/author_animated_hands.py bakes the turn), so
    // the global turned them a second time and the weapon crossed the view
    // diagonally.
    //
    // `hasFraming` false -- every procedurally-authored rig -- leaves the global
    // in charge, so nothing that worked before this moves.
    bool hasFraming = false;
    glm::vec3 framingOffset{0.0f};
    glm::vec3 framingRotationDegrees{0.0f};
    float framingScale = 1.0f;

    std::vector<ViewmodelSocketDef> sockets;

    // The sprite-mode equivalent of `sockets`: the player's hand layers, drawn
    // under whatever a sprite weapon composites over them.
    //
    // Authored here rather than per weapon for exactly the reason the socket
    // list is: replacing the player's hands must be one file, not an edit to
    // every weapon. A sprite weapon may still author a hand layer of its own --
    // it is the same type -- but this is what it gets for free.
    //
    // Empty by default. The shipped rig is skinned, so nothing uses these until
    // a weapon asks for `presentation = "sprite"`.
    std::vector<ViewmodelSpriteLayer> spriteLayers;
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

// Every rig the viewmodel can wear, and which one it wears by default.
//
// The hands used to be one rig because there was one rig: a downloaded arms
// mesh with a fixed joint list. The built-in content library now ships fifteen
// -- six human skin/suit pairings, three gloves, three alien, three werewolf --
// and they are NOT interchangeable at the joint level: the alien arm has three
// fingers, so a socket list is a property of a rig rather than of the hands in
// general.
//
// So the file grew an array and this grew a container, and the rest of the game
// still asks for one HandsDefinition. Swapping hands is `active(id)`, which is
// what the debug panel and the editor's preview both drive.
struct HandsLibrary {
    std::vector<HandsDefinition> rigs;
    std::string defaultRig;

    // Null when the id is unknown -- the caller decides whether that is worth
    // reporting; a saved preference naming a rig that has since been removed
    // is not an error, it is a fallback.
    const HandsDefinition* find(std::string_view id) const;
    // `defaultRig` if it resolves, else the first rig, else the shipped
    // default. Never returns something the renderer cannot load.
    const HandsDefinition& active() const;
    std::vector<std::string> ids() const;
};

// `schema = 2` with an `[[rig]]` array, or the single `[hands]` table that came
// before it. Both are accepted, and the older form produces a one-rig library,
// so a project pinned to its own hands file keeps working unchanged.
bool loadHandsLibrary(const std::string& tomlPath, HandsLibrary& out);
bool parseHandsLibrary(const char* tomlSource, HandsLibrary& out);

} // namespace game
