#pragma once
#include <glm/glm.hpp>

namespace eng::ecs {

// Travel around a point, applied to the entity's Transform by `orbitSystem`.
//
// `Spin` turns a thing where it stands. This moves it along a ring, and it does
// so *without a pivot*: the entity carries its own centre and radius, so any
// entity can orbit anything without an extra node in the scene and without
// being parented to it.
//
// The pivot rig -- an empty entity with a Spin, children parented to it -- still
// exists and is still right when a *group* has to revolve as one, because a
// parent is what makes several things share a motion. Orbit is the single-entity
// case, which is nearly every case: it was three entities and a parent link for
// one moving camera, and the offset had to be re-derived by hand every time the
// radius changed because the radius WAS the child's transform.
//
// Composes with Spin. A moon is an entity with both: Orbit carries it round the
// planet, Spin turns it on its own axis. That works because Orbit writes the
// position and, unless it is aiming the entity, leaves the rotation alone.
struct Orbit {
    // What the entity circles, in the same frame as its own Transform -- its
    // parent's, or the world when it has none. So an orbit inside a rig moves
    // with the rig, and one at the top level is a place in the level.
    glm::vec3 centre{0.0f};
    // The ring's normal. +Y is a level circle; tilt it for an inclined orbit.
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float radius = 5.0f;
    float degreesPerSecond = 30.0f;
    // Where on the ring it starts. Two entities on one ring with the same rate
    // and no phase occupy the same point forever.
    float phaseDegrees = 0.0f;
    // Along the axis from the centre: the ring's height above what it circles.
    // Separate from `centre` so raising a camera does not mean recomputing the
    // point it is looking at.
    float height = 0.0f;

    // What the entity does with its facing while it travels.
    enum Facing : int {
        // Keep the authored rotation. Leaves the door open for Spin, and is
        // what a prop being carried round wants.
        Free = 0,
        // Look at the centre. What a camera orbiting a subject is for, and the
        // reason the pivot rig existed: a parented camera got this by accident
        // of composition, and only for a centre at the pivot's origin.
        Centre = 1,
        // Look along the direction of travel, like something flying the ring.
        Travel = 2,
    };
    // Held as int, not the enum: the reflection layer's field types are the
    // ones a byte stream and an ImGui widget both understand, and an enum that
    // serialises as an int cannot acquire a value the reader has never heard of.
    int facing = Free;

    // Accumulated degrees. Runtime state, deliberately not reflected: it is
    // where the entity currently is, not how it was authored, and a saved one
    // would reload mid-arc.
    float travelled = 0.0f;
};

} // namespace eng::ecs
