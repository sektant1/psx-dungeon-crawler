#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <string>

namespace eng::ecs {

// Filled in by SceneSync for an entity with a ParticleEmitter; callers never
// set it. Separate from the emitter for the same reason NodeRef is separate
// from MeshRenderer: authored intent and runtime handle have different
// lifetimes, and only one of the two is worth writing to a file.
struct ParticlesRef {
    ParticlesHandle handle;
    // Last authored request applied to the handle. SceneSync restarts the
    // emitter when one changes instead of leaving an old effect attached.
    std::string effect;
    glm::vec3 offset{0.0f};
    float scale = 1.0f;
};

} // namespace eng::ecs
