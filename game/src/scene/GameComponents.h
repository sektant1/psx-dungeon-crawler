#pragma once

#include <eng/Physics.h> // eng::ShapeKind, eng::CollisionLayer
#include <eng/ecs/Components.h> // eng::ecs::Collider
#include "GameCollision.h"
#include "ViewmodelRig.h" // game::ViewmodelRig, authored on the player camera
#include "audio/ActorSounds.h"

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

// game::ViewmodelRig is defined in ViewmodelRig.h, included above: it is a
// runtime tuning struct as well as an authored component, and the runtime
// motion composer must not have to include this header (which pulls in Jolt)
// to read it. Named here because this is where a reader looks for the scene's
// component set.
//
// It rides on the same entity as eng::ecs::FirstPersonController -- the camera
// that is the player's eye -- so "how this level's player moves" and "where
// this level's hands sit" are two components on one entity, in the inspector,
// beside each other.

// game::Actor and game::ActorSounds are defined in audio/ActorSounds.h, which
// is included above: they ride on the combat registry as well as this one, and
// that side does not link the physics types this header pulls in. Named here
// because this is where a reader looks for the scene's component set.

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

// A person worth talking to. `id` keys into dialogue.toml, traders.toml and
// the quest book at once, because those three name the same person: the
// blacksmith you buy from is the blacksmith you talk to is the blacksmith who
// wanted six iron ingots.
//
// Deliberately just the id. Their name, their trade, what they say and whether
// they have anything for you are all facts about the person, which live in the
// content that describes them -- an authored entity that restated any of it
// would be a second place for those to be wrong. What the *scene* decides is
// only where they stand and which way they face, and the transform already
// says that.
struct Npc {
    std::string id;
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

// The look a level is lit and graded with: a table in palettes.toml. Cooked
// onto one entity because a .map is a flat list of entities and has nowhere
// else to put a fact about the whole level.
//
// The runtime reads the first one it finds; the cooker only ever writes one.
struct SceneEnvironment {
    std::string palette;
};

} // namespace game
