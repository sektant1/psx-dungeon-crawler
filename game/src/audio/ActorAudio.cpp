#include "audio/ActorAudio.h"

#include "enemy/EnemyComponents.h"

namespace game {
namespace {

// The type's table, when the entity has a type that carries one. Today that is
// an enemy's EnemyDef; a player and an NPC have no type row yet, so they
// resolve straight from the convention -- which is exactly what the fallback
// chain is for, and why adding an NPC library later is one line here.
const ActorSoundSet* typeSounds(const entt::registry& registry,
                                entt::entity entity)
{
    if (const EnemyTag* tag = registry.try_get<EnemyTag>(entity);
        tag && tag->def)
        return &tag->def->sounds;
    return nullptr;
}

} // namespace

const Actor* ActorAudio::actorOf(entt::entity entity) const
{
    if (!mRegistry || entity == entt::null || !mRegistry->valid(entity))
        return nullptr;
    return mRegistry->try_get<Actor>(entity);
}

std::string ActorAudio::cueFor(entt::entity entity, ActorAction action) const
{
    const Actor* actor = actorOf(entity);
    if (!actor || !actorPerforms(actor->kind, action))
        return {};
    const ActorSounds* overrides = mRegistry->try_get<ActorSounds>(entity);
    std::string convention;
    return std::string(resolveActorCue(overrides ? &overrides->set : nullptr,
                                       typeSounds(*mRegistry, entity),
                                       actor->kind, action, convention));
}

bool ActorAudio::play(entt::entity entity, ActorAction action,
                      glm::vec3 position, bool firstPerson)
{
    return play(entity, action, position, std::string_view(), firstPerson);
}

bool ActorAudio::play(entt::entity entity, ActorAction action,
                      glm::vec3 position, std::string_view cueOverride,
                      bool firstPerson)
{
    if (!mAudio)
        return false;
    const Actor* actor = actorOf(entity);
    if (!actor)
        return false; // scenery, a projectile, a prop: it performs nothing
    if (cueOverride.empty() && !actorPerforms(actor->kind, action))
        return false;

    std::string convention;
    std::string_view cue = cueOverride;
    if (cue.empty()) {
        const ActorSounds* overrides = mRegistry->try_get<ActorSounds>(entity);
        cue = resolveActorCue(overrides ? &overrides->set : nullptr,
                              typeSounds(*mRegistry, entity), actor->kind,
                              action, convention);
    }
    if (cue.empty())
        return false;

    AudioEmission emission;
    emission.position = position;
    // Head-locked when it is the player's own body making the noise. An impact
    // is not: it happens where the shot landed, and forcing that 2D would put
    // a hit across the room inside the player's skull.
    if (firstPerson || (actor->kind == ActorKind::Player &&
                        actorActionInfo(action).ownBody))
        emission.spatial = SpatialOverride::Force2D;
    return mAudio->emit(audioCueId(cue), emission) != nullptr;
}

} // namespace game
