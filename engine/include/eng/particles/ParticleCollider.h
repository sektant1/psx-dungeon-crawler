#pragma once
#include <glm/glm.hpp>

#include <string>

namespace eng {

// World-space request for a projected mark, drained by the runtime each frame.
// The simulation never touches the decal system: it only says where a mark
// belongs and which profile describes it.
struct DecalRequest {
    std::string profile;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

// The particle simulation's only view of the world, and the reason the whole
// simulation stays headlessly testable: tests supply a stub, the game supplies
// a Jolt adapter, and neither the sim nor the renderer ever links physics.
//
// This is a public header rather than a detail of the simulation because it is
// the seam the application wires: Renderer::setParticleCollider takes one, so
// the type has to be nameable from outside the engine.
struct IParticleCollider {
    virtual ~IParticleCollider() = default;

    // Trace the swept segment. Returns true and fills the hit on contact; the
    // normal must point out of the surface that was hit.
    virtual bool sweep(glm::vec3 from, glm::vec3 to, glm::vec3& hitPos,
                       glm::vec3& hitNormal) = 0;
};

} // namespace eng
