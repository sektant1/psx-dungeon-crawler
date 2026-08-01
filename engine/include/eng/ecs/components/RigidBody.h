#pragma once

namespace eng::ecs {

// Dynamic rigid body: beside a Collider, this is what hands the entity's
// transform to the physics simulation. Its pose then flows *out* of physics,
// not in -- PhysicsSync stops writing the component's transform onto it, unless
// KinematicControl says gameplay is steering.
struct RigidBody {
    float mass = 1.0f;
    // 0 for anything that must ignore gravity (a hovering orb, a magical
    // projectile); 1 is ordinary.
    float gravityFactor = 1.0f;
    float friction = 0.5f;
    // Jolt combines both bodies' restitution, so this defaults to 0: a grounded
    // dungeon wants props to thud and settle, not rebound.
    float restitution = 0.0f;
    // Kinematic bodies push others and are pushed by nothing -- a moving
    // platform, an enemy that must hold its spacing through a windup.
    bool kinematic = false;
    // Swept collision, for anything fast enough to tunnel through a wall in one
    // step. Costs more; opt in.
    bool continuous = false;
};

} // namespace eng::ecs
