#pragma once
#include <eng/Handles.h>

namespace eng::ecs {

// The physics body backing this entity. Populated by PhysicsSync; callers never
// set it. Its presence is how the sync knows a body already exists, so writing
// one by hand strands a body in the world.
struct BodyRef {
    BodyHandle handle;
};

} // namespace eng::ecs
