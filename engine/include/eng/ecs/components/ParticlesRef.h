#pragma once
#include <eng/Handles.h>

namespace eng::ecs {

// Filled in by SceneSync for an entity with a ParticleEmitter; callers never
// set it. Separate from the emitter for the same reason NodeRef is separate
// from MeshRenderer: authored intent and runtime handle have different
// lifetimes, and only one of the two is worth writing to a file.
struct ParticlesRef {
    ParticlesHandle handle;
};

} // namespace eng::ecs
