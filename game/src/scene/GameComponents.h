#pragma once

#include <eng/Physics.h> // eng::ShapeKind, eng::CollisionLayer
#include <eng/ecs/Components.h> // eng::ecs::Collider
#include "GameCollision.h"

#include <glm/glm.hpp>

#include <string>

// Gameplay components authored in the editor and read by the runtime. Core
// scene components (Name, Transform, MeshRenderer, LightRef) live in
// engine/include/eng/ecs/Components.h and are registered alongside these.
namespace game {

// Static collision volume. Emitted as a Jolt body by eng::ecs::PhysicsSync
// (Plan 3). The component itself is engine-owned now -- a collision volume is
// not gameplay -- and only the layer values it carries are ours.
using Collider = eng::ecs::Collider;
static_assert(layer::Static == 0, "eng::ecs::Collider defaults to layer 0, "
                                  "which must stay the game's static layer");

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
// Deciding that a trigger is a sensor body on the trigger layer is game
// policy, so MapRuntime gives each one a sensor Collider; the engine only
// knows how to turn a Collider into a body.
struct Trigger {
    eng::ShapeKind shape = eng::ShapeKind::Box;
    glm::vec3 size{1.0f};
    std::string event;
};

// Typed authored placement consumed by game-side scene assembly after the
// static .map has loaded. Examples are the boss target, reward shrine, dynamic
// physics props, and named feature-test stations. The engine only sees a
// serializable string; interpretation remains game policy.
struct SceneMarker {
    std::string type;
};

} // namespace game
