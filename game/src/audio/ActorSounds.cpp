#include "audio/ActorSounds.h"

namespace game {
namespace {

constexpr std::uint8_t kNonPlayer =
    actorKindBit(ActorKind::Npc) | actorKindBit(ActorKind::Enemy);
constexpr std::uint8_t kPlayerOnly = actorKindBit(ActorKind::Player);
constexpr std::uint8_t kNpcOnly = actorKindBit(ActorKind::Npc);

// The table. Every `hint` names the moment the runtime emits the cue, because
// that is the only thing an author needs in order to choose a clip -- and
// because a row whose hint cannot be written is a row with no call site, which
// is exactly the kind of dead field this table exists to avoid.
const std::array<ActorActionInfo, kActorActionCount> kActions = {{
    {ActorAction::Spawn, "spawn", "Spawn",
     "when the actor first appears in the level", kAnyActor},
    {ActorAction::Idle, "idle", "Idle",
     "occasional breathing/growl while it has no target", kNonPlayer},
    {ActorAction::Alert, "alert", "Alert",
     "the beat it notices the player", kNonPlayer},
    {ActorAction::Footstep, "footstep", "Footstep",
     "each step while moving on the ground", kAnyActor},
    {ActorAction::Jump, "jump", "Jump", "leaving the ground", kPlayerOnly},
    {ActorAction::Land, "land", "Land", "touching down after a fall",
     kAnyActor},
    {ActorAction::Telegraph, "telegraph", "Attack windup",
     "the readable wind-up before a swing; an attack's own telegraph_sound "
     "overrides this",
     kNonPlayer},
    {ActorAction::Attack, "attack", "Attack",
     "the swing or shot leaving the actor", kAnyActor},
    {ActorAction::Impact, "impact", "Impact",
     "its attack landing on something", kAnyActor, /*ownBody=*/false},
    {ActorAction::Hurt, "hurt", "Hurt", "taking damage and surviving",
     kAnyActor},
    {ActorAction::Block, "block", "Block",
     "a deflect negating an incoming hit", kAnyActor},
    {ActorAction::Dodge, "dodge", "Dodge", "a dodge/dash starting", kAnyActor},
    {ActorAction::Death, "death", "Death", "the killing blow", kAnyActor},
    {ActorAction::Interact, "interact", "Interact",
     "the player interacting with this NPC", kNpcOnly},
}};

} // namespace

const char* actorKindName(ActorKind kind)
{
    switch (kind) {
    case ActorKind::Player: return "player";
    case ActorKind::Npc:    return "npc";
    case ActorKind::Enemy:  return "enemy";
    }
    return "npc";
}

const char* actorKindCuePrefix(ActorKind kind)
{
    return actorKindName(kind);
}

bool parseActorKind(std::string_view name, ActorKind& out)
{
    if (name == "player") {
        out = ActorKind::Player;
        return true;
    }
    if (name == "npc") {
        out = ActorKind::Npc;
        return true;
    }
    if (name == "enemy") {
        out = ActorKind::Enemy;
        return true;
    }
    return false;
}

const std::array<ActorActionInfo, kActorActionCount>& actorActions()
{
    return kActions;
}

const ActorActionInfo& actorActionInfo(ActorAction action)
{
    return kActions[static_cast<std::size_t>(action)];
}

const ActorActionInfo* findActorAction(std::string_view id)
{
    for (const ActorActionInfo& info : kActions)
        if (id == info.id)
            return &info;
    return nullptr;
}

bool actorPerforms(ActorKind kind, ActorAction action)
{
    return (actorActionInfo(action).kinds & actorKindBit(kind)) != 0;
}

bool ActorSoundSet::empty() const
{
    for (const std::string& cue : cues)
        if (!cue.empty())
            return false;
    return true;
}

void ActorSoundSet::inheritFrom(const ActorSoundSet& other)
{
    for (std::size_t i = 0; i < kActorActionCount; ++i)
        if (cues[i].empty())
            cues[i] = other.cues[i];
}

bool operator==(const ActorSoundSet& a, const ActorSoundSet& b)
{
    return a.cues == b.cues;
}

std::string actorConventionCue(ActorKind kind, ActorAction action)
{
    std::string cue = actorKindCuePrefix(kind);
    cue += '.';
    cue += actorActionInfo(action).id;
    return cue;
}

std::string_view resolveActorCue(const ActorSoundSet* entity,
                                 const ActorSoundSet* type, ActorKind kind,
                                 ActorAction action,
                                 std::string& conventionStorage)
{
    if (entity && !entity->cue(action).empty())
        return entity->cue(action);
    if (type && !type->cue(action).empty())
        return type->cue(action);
    conventionStorage = actorConventionCue(kind, action);
    return conventionStorage;
}

} // namespace game
