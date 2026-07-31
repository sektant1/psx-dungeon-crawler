#pragma once
#include "combat/CombatVocabulary.h"

#include <string>

namespace eng {
class Renderer;
class Physics;
class Input;
}

namespace game {

// Shared, non-owning references handed to game systems so they don't each
// capture the world in ad-hoc lambdas. All referents outlive the game loop
// (owned by main). Systems read what they need and nothing more.
struct GameContext {
    eng::Renderer& renderer;
    eng::Physics& physics;
    eng::Input& input;
    // No asset root here any more: a system that needs content asks
    // game::assetPath("weapons.toml") (GameAssets.h) and lets the resolver
    // walk the mount list. Threading a root through the context made every
    // system that touches a file also a system that knows where the tree is.
    // Damage channels + schools of magic, from magic.toml. Systems that
    // turn an authored name into an id or a palette go through this.
    const CombatVocabulary& vocabulary;
};

} // namespace game
