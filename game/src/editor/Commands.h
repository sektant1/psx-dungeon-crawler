#pragma once

#include "CommandStack.h"

#include <eng/ecs/Components.h>

#include <entt/entt.hpp>

#include <string>
#include <vector>

namespace editor {

Command makeCreateEntity(entt::registry& reg, std::string name,
                         entt::entity* outEntity);

Command makeSetTransform(entt::registry& reg, entt::entity e,
                         eng::ecs::Transform next);

Command makeDeleteEntity(entt::registry& reg, entt::entity e);

// Bundle several commands into one undo/redo step. apply runs them in order;
// revert runs them in reverse. Used for group gizmo drags (one transform
// command per selected entity, undone as a unit).
Command makeComposite(std::vector<Command> commands);

} // namespace editor
