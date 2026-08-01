#pragma once

namespace eng::ecs {

// Seconds this entity has left. `lifetimeSystem` counts it down and destroys the
// entity -- and its subtree -- when it reaches zero.
//
// The alternative every engine grows without it is a despawn timer per system:
// one in the projectile loop, one in the corpse fader, one in the VFX spawner,
// each with its own idea of what "expired" means and its own way of forgetting.
// A component makes "this thing is temporary" a property of the thing, so a
// muzzle flash, a bolt that missed and a decal all die by the same three lines.
//
// The subtree goes with it deliberately: a projectile with a trail emitter
// parented under it is one object, and leaving the trail behind as an orphan is
// the bug this saves.
struct Lifetime {
    float remaining = 1.0f;
};

} // namespace eng::ecs
