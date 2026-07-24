#pragma once

#include <eng/Physics.h> // eng::ShapeKind, eng::BodyLayer

#include <glm/glm.hpp>

#include <string>

// Gameplay components authored in the editor and read by the runtime. Core
// scene components (Name, Transform, MeshRenderer, LightRef) live in
// engine/include/eng/ecs/Components.h and are registered alongside these.
namespace game {

// Static collision volume. Emitted as a Jolt body by PhysicsSync (Plan 3).
struct Collider {
    eng::ShapeKind shape = eng::ShapeKind::Box;
    glm::vec3 size{0.5f}; // half-extents (box) / radius in x (sphere)
    eng::BodyLayer layer = eng::BodyLayer::Static;
};

// Unique player start.
struct PlayerSpawn {};

// Level exit / down-portal. yawDegrees orients the arrival facing.
struct Exit {
    float yawDegrees = 0.0f;
};

// Enemy placement; type keys into the enemy factory.
struct EnemySpawn {
    std::string type;
};

// Loot / item placement; type keys into the pickup factory.
struct Pickup {
    std::string type;
};

// Event volume (WC3-style region). event keys into the trigger dispatch.
struct Trigger {
    eng::ShapeKind shape = eng::ShapeKind::Box;
    glm::vec3 size{1.0f};
    std::string event;
};

} // namespace game
