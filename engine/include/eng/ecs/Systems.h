#pragma once

namespace eng::ecs {

class World;

// The systems that make the engine's own behavioural components do something.
//
// Free functions taking (World&, dt), which is the shape *Game Engine
// Architecture* §1.5.15 describes and the one this engine already uses: no
// registration, no update list, no virtual update() on an object. A caller that
// wants a behaviour calls its system; one that does not, does not link it.
//
// Each is written to be safe to call on a World with none of the relevant
// components -- iterating an empty view costs a branch -- so the game's frame
// can call all of them unconditionally.
//
// Deliberately *not* called by World::sync(). sync() reconciles views with the
// registry and must stay callable from a tool that is not running a game: the
// editor syncs its preview world every frame and would otherwise watch authored
// entities spin and expire while it tried to place them.
void lifetimeSystem(World&, float dt);
void spinSystem(World&, float dt);
// Runs AFTER spinSystem, and that order is the contract between the two: Spin
// accumulates a rotation, Orbit then writes a position and -- only when it is
// aiming the entity -- replaces that rotation. So Spin + Orbit(Free) is a moon,
// and Orbit(Centre) is a camera whose facing Spin cannot fight over.
void orbitSystem(World&, float dt);
void lightAnimationSystem(World&, float dt);

// All of them, in the order a frame wants them: animate, then expire. Call once
// per frame before World::sync().
void tickComponentSystems(World&, float dt);

} // namespace eng::ecs
