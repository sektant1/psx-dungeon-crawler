#pragma once
#include <cstdint>

namespace eng::ecs {

// Which lifetime group this entity belongs to. Group 0 is "no group": it lives
// as long as the World does.
//
// A World outlives the levels streamed through it -- the player, their weapons
// and their progress are not the level's to destroy -- so a level transition
// needs to remove exactly what the level added and nothing else. Stamping a
// group at creation (World::setActiveGroup) and destroying it wholesale
// (World::destroyGroup) is how, and it is the only piece of lifetime policy
// the engine imposes.
struct EntityGroup {
    uint32_t value = 0;
};

} // namespace eng::ecs
