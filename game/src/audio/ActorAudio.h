#pragma once

#include "audio/ActorSounds.h"
#include "audio/GameAudio.h"

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

namespace game {

// Plays an actor's action, whatever that actor happens to be.
//
// Call sites say WHAT HAPPENED -- "this entity died", "this entity was hit" --
// and this resolves the cue by the override chain the author expects:
//
//     the placed entity's own table   (ActorSounds component)
//         -> its type's table         (EnemyDef::sounds)
//             -> the convention       ("enemy.death")
//
// One place rather than at every emission, because the chain is the part that
// is easy to get subtly wrong: main.cpp used to read `def->visual.deathSound`
// directly, which meant a placed entity could not differ from its type, and a
// player or an NPC had no equivalent path at all.
//
// A cue the catalog does not define is a documented no-op (GameAudioSystem
// counts it), so an actor whose kind names a cue nobody authored is silent
// rather than a crash -- which is what makes the convention safe to rely on.
class ActorAudio {
public:
    ActorAudio(GameAudioSystem& audio, entt::registry& registry)
        : mAudio(&audio), mRegistry(&registry)
    {
    }

    // What the registry says this entity is. Nothing for scenery, projectiles
    // and props -- the caller then plays nothing, which is why a hit on a wall
    // does not emit an actor grunt.
    const Actor* actorOf(entt::entity entity) const;

    // The cue that would play, without playing it. Exposed for the debug panel
    // and for callers that need to know whether anything is authored.
    std::string cueFor(entt::entity entity, ActorAction action) const;

    // Play `action` for `entity` at `position`. Returns false when the entity
    // is not an actor, when its kind does not perform the action, or when
    // nothing resolved.
    //
    // `firstPerson` forces the emission 2D: the player's own foley must not pan
    // with their head, which is the same rule GameAudio's player cues follow.
    bool play(entt::entity entity, ActorAction action, glm::vec3 position,
              bool firstPerson = false);

    // Same, with the cue chosen by the caller when it has a more specific one
    // than the action's -- an attack's own `telegraph_sound`, say. An empty
    // `override` falls straight through to the action's chain, so a call site
    // never has to branch on whether the specific cue was authored.
    bool play(entt::entity entity, ActorAction action, glm::vec3 position,
              std::string_view cueOverride, bool firstPerson = false);

private:
    GameAudioSystem* mAudio = nullptr;
    entt::registry* mRegistry = nullptr;
};

} // namespace game
