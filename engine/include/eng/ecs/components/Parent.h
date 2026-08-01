#pragma once
#include <entt/entt.hpp>

namespace eng::ecs {

// Parent link. Absent, or entt::null, means the entity is a scene root.
//
// Maintained only through World::setParent, which rejects cycles and keeps the
// other end (Children) in step. Writing it directly leaves the two halves
// disagreeing, and the resolve pass recurses on the half that is wrong.
struct Parent {
    entt::entity value{entt::null};
};

} // namespace eng::ecs
