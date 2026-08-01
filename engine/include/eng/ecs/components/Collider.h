#pragma once
#include <eng/Physics.h> // eng::ShapeKind, eng::CollisionLayer

#include <glm/glm.hpp>

namespace eng::ecs {

// A shape that occupies space. Materialised as a physics body by PhysicsSync.
//
// A Collider alone is static world geometry; a RigidBody beside it is what
// hands the transform to the simulation. That split is the useful one: this
// component says *what shape*, RigidBody says *whether it moves*, so authoring
// a prop that falls over is adding one component to a thing that already
// collides rather than building a different kind of object.
//
// What a collider *means* is not decided here: `layer` indexes the
// application's own layer table -- the engine has no taxonomy of its own -- and
// `sensor` makes the body a non-blocking overlap volume, which is what an event
// region (a door trigger, a damage zone) is built out of.
struct Collider {
    ShapeKind shape = ShapeKind::Box;
    glm::vec3 size{0.5f}; // half-extents (box) / radius in x (sphere)
    CollisionLayer layer = 0;
    bool sensor = false;
};

} // namespace eng::ecs
