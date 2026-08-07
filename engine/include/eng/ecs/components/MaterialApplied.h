#pragma once
#include <string>

namespace eng::ecs {

// The override SceneSync last pushed, so it only pushes on a change. Applying
// one re-resolves the material and re-attaches every mesh under the node,
// which costs far more than a draw -- doing it every frame would make a
// material override the most expensive component in the engine.
struct MaterialApplied {
    std::string material;
};

} // namespace eng::ecs
