#pragma once

namespace eng::ecs {

// Tag: gameplay drives this dynamic body's transform, so PhysicsSync keeps
// pushing the component's pose onto it instead of letting the simulation own
// it. For a kinematic RigidBody something steers -- a moving platform, an enemy
// holding its spacing. Without it, a dynamic body's pose flows the other way,
// out of the simulation, and writing to the Transform does nothing.
struct KinematicControl {};

} // namespace eng::ecs
