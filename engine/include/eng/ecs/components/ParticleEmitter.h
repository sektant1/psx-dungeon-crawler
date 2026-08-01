#pragma once
#include <glm/glm.hpp>

#include <string>

namespace eng::ecs {

// A particle effect that plays from this entity, named in the particle library.
// Attached by SceneSync when the entity first gets a node, stopped when the
// component is removed or the entity dies.
//
// This is what turns VFX into scene data: an author picks an entity, adds an
// emitter, names an effect. Before it, every effect in the game was spawned by
// a line of C++ that had to know both the effect and the thing it belonged to.
struct ParticleEmitter {
    std::string effect;     // library name; empty plays nothing
    glm::vec3 offset{0.0f}; // local offset from the entity's node
    bool playing = true;    // false attaches nothing until it is set
    float scale = 1.0f;
};

} // namespace eng::ecs
