#pragma once
#include <entt/entt.hpp>

#include <vector>

namespace eng::ecs {

// The other end of Parent, maintained by World::setParent so a subtree walk is
// O(children) instead of a scan of every entity in the world.
struct Children {
    std::vector<entt::entity> value;
};

} // namespace eng::ecs
