#pragma once
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
    const std::string& assets; // app asset root (APP_ASSET_DIR)
};

} // namespace game
