#pragma once
#include <eng/ecs/Components.h>

namespace game {
// The body backing an entity is engine bookkeeping now: eng::ecs::PhysicsSync
// fills it in, exactly as SceneSync fills in NodeRef. Aliased so game code
// keeps naming it `game::BodyRef`.
using BodyRef = eng::ecs::BodyRef;
} // namespace game
