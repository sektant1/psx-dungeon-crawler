#pragma once
#include <glm/glm.hpp>

namespace eng::ecs {

// Per-frame light colour override (linear, energy pre-multiplied). On an entity
// that also has a LightRef, SceneSync pushes this to the light every frame --
// which is what makes torch flicker and glow pulses gameplay writing a
// component rather than gameplay calling the renderer.
struct LightColour {
    glm::vec3 value{1.0f};
};

} // namespace eng::ecs
