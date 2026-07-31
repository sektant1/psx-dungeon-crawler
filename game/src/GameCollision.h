#pragma once

#include <eng/Physics.h>

// The game's collision layers. The engine has no taxonomy of its own -- it
// takes a layer table and a collision matrix -- so this header is the single
// place that says what a layer means in this dungeon crawler, and the only
// place that has to change to add one.
namespace game::layer {

inline constexpr eng::CollisionLayer Static = 0;     // level geometry
inline constexpr eng::CollisionLayer Player = 1;
inline constexpr eng::CollisionLayer Prop = 2;       // crates, barrels, dummies
inline constexpr eng::CollisionLayer Projectile = 3; // arrows, bolts, spells
inline constexpr eng::CollisionLayer Trigger = 4;    // interaction volumes

// Query masks, named for what they ask rather than what they contain. A cast
// that wants "anything a swing can connect with" says so once here instead of
// spelling out the layer set at every call site.
inline constexpr eng::CollisionMask kSolid =
    eng::layerMask(Static) | eng::layerMask(Prop);
inline constexpr eng::CollisionMask kHittable =
    eng::layerMask(Prop) | eng::layerMask(Player);

// The world's collision matrix. Everything interacts except:
//   - static/static: two pieces of level geometry can never move into contact;
//   - projectile/projectile: arrows pass through each other;
//   - player/projectile: player-owned physics projectiles cannot hit their owner;
//   - trigger against props and projectiles: interaction volumes exist for the
//     player, and a barrel rolling through a door trigger must not open it.
// The tuning half of the physics setup, kept separate from the layer table so
// callers that only need a world (tests, the sim harness) get the same feel
// defaults without having to own a config file. main.cpp overrides these from
// the [physics] section of game.toml.
struct PhysicsTuning {
    float gravity = -18.0f;         // m/s^2, downward
    float characterPushImpulse = 2.0f;
    bool multithreaded = false;     // off: determinism beats ~0.2ms
};

inline eng::PhysicsSetup physicsSetup(const PhysicsTuning& tuning = {})
{
    eng::PhysicsSetup s;
    s.layers.resize(5);
    s.layers[Static] = {"static", /*moving=*/false, {0.5f, 0.5f, 0.5f}};
    s.layers[Player] = {"player", true, {0.2f, 0.8f, 1.0f}};
    s.layers[Prop] = {"prop", true, {0.2f, 1.0f, 0.2f}};
    s.layers[Projectile] = {"projectile", true, {1.0f, 1.0f, 0.2f}};
    // Triggers never move once placed, so they belong in the non-moving
    // broad-phase bucket with the level.
    s.layers[Trigger] = {"trigger", false, {1.0f, 0.4f, 1.0f}};

    s.collideAll();
    s.setPair(Static, Static, false);
    s.setPair(Projectile, Projectile, false);
    s.setPair(Player, Projectile, false);
    s.setPair(Trigger, Trigger, false);
    s.setPair(Trigger, Prop, false);
    s.setPair(Trigger, Projectile, false);

    s.characterLayer = Player;

    s.gravity = {0.0f, tuning.gravity, 0.0f};
    s.characterPushImpulse = tuning.characterPushImpulse;
    s.multithreaded = tuning.multithreaded;
    return s;
}

} // namespace game::layer
