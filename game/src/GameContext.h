#pragma once
#include "combat/CombatVocabulary.h"

#include <string>

namespace eng {
class Renderer;
class Physics;
class Input;
}

namespace game {

namespace actor { class ActorRig; }

// Shared, non-owning references handed to game systems so they don't each
// capture the world in ad-hoc lambdas. All referents outlive the game loop
// (owned by main). Systems read what they need and nothing more.
struct GameContext {
    eng::Renderer& renderer;
    eng::Physics& physics;
    eng::Input& input;
    // No asset root here any more: a system that needs content asks
    // game::assetPath("config/weapons.toml") (GameAssets.h) and lets the resolver
    // walk the mount list. Threading a root through the context made every
    // system that touches a file also a system that knows where the tree is.
    // Damage channels + schools of magic, from magic.toml. Systems that
    // turn an authored name into an id or a palette go through this.
    const CombatVocabulary& vocabulary;
    // The body. One rigged humanoid -- skeleton, clips and skinned mesh --
    // shared by the player's avatar, every enemy and every NPC, so a room of
    // thirty actors is thirty skin instances over one upload rather than
    // thirty of everything.
    //
    // A reference on the context rather than a global or a per-system load:
    // the three systems that need it have no other place to meet, and making
    // each load its own would triple the memory and let them drift onto
    // different skeletons. Invalid until the rig has loaded; every consumer
    // checks and falls back to a primitive body, so a missing or broken rig
    // costs the game its animation and not its ability to start.
    const actor::ActorRig& humanoid;
};

} // namespace game
