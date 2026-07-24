#pragma once

#include "CommandStack.h"

#include <eng/ecs/Components.h>

#include <entt/entt.hpp>

#include <string>

namespace editor {

Command makeCreateEntity(entt::registry& reg, std::string name,
                         entt::entity* outEntity);

Command makeSetTransform(entt::registry& reg, entt::entity e,
                         eng::ecs::Transform next);

Command makeDeleteEntity(entt::registry& reg, entt::entity e);

} // namespace editor
