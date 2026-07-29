#pragma once

#include <eng/ecs/ComponentRegistry.h>

namespace mapio {

// The game's component table: the engine's own types plus this game's
// gameplay markers (collider, spawn points, exits, pickups, triggers), which
// take stable ids from eng::ecs::kFirstApplicationTypeId up. Built once and
// shared -- the serialiser, the editor inspector and the add-component menu
// all iterate the same list.
const eng::ecs::ComponentRegistry& coreRegistry();

} // namespace mapio
