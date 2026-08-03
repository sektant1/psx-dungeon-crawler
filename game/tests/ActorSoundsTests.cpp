// The actor/action sound vocabulary: which kinds perform which actions, and
// the override chain a cue resolves through.
//
// The chain is the part worth pinning. It is three deep -- placed entity, then
// actor type, then the "<kind>.<action>" convention -- and every level of it is
// authored somewhere different, so a regression in it is a sound that silently
// stops being overridable rather than a crash.
#include "../src/audio/ActorSounds.h"

#include <cstdio>
#include <string>

using namespace game;

static int failures = 0;
static void check(bool condition, const char* what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main()
{
    // --- vocabulary --------------------------------------------------------
    check(actorActions().size() == kActorActionCount,
          "the action table covers every action in the enum");
    for (const ActorActionInfo& info : actorActions()) {
        check(findActorAction(info.id) == &info,
              "every action is findable by its own id");
        check(info.kinds != 0, "an action no kind performs is a dead row");
        check(info.hint != nullptr && *info.hint != '\0',
              "every action says when it plays");
    }
    check(findActorAction("nonsense") == nullptr,
          "an unknown action id resolves to nothing");

    // Kind gating. These are the asymmetries the inspector shows and hides.
    check(actorPerforms(ActorKind::Enemy, ActorAction::Telegraph),
          "an enemy telegraphs its swings");
    check(!actorPerforms(ActorKind::Player, ActorAction::Telegraph),
          "the player does not telegraph");
    check(actorPerforms(ActorKind::Player, ActorAction::Jump),
          "the player jumps");
    check(!actorPerforms(ActorKind::Enemy, ActorAction::Jump),
          "enemies do not jump");
    check(actorPerforms(ActorKind::Npc, ActorAction::Interact),
          "an NPC can be interacted with");
    check(!actorPerforms(ActorKind::Enemy, ActorAction::Interact),
          "an enemy is not an NPC to talk to");

    // Impact is the one action that does not come from the actor's own body,
    // which is what keeps a player's projectile hit from being forced 2D.
    check(!actorActionInfo(ActorAction::Impact).ownBody,
          "an impact happens where the shot landed");
    check(actorActionInfo(ActorAction::Footstep).ownBody,
          "a footstep happens under the actor");

    // --- parsing -----------------------------------------------------------
    {
        ActorKind kind = ActorKind::Player;
        check(parseActorKind("enemy", kind) && kind == ActorKind::Enemy,
              "\"enemy\" parses");
        check(parseActorKind("npc", kind) && kind == ActorKind::Npc,
              "\"npc\" parses");
        check(!parseActorKind("monster", kind),
              "an unknown kind is refused rather than defaulted");
        check(kind == ActorKind::Npc, "a refused parse leaves the value alone");
    }

    // --- conventions -------------------------------------------------------
    check(actorConventionCue(ActorKind::Enemy, ActorAction::Death) ==
              "enemy.death",
          "the convention is <kind>.<action>");
    check(actorConventionCue(ActorKind::Player, ActorAction::Footstep) ==
              "player.footstep",
          "the player's convention matches the shipped cue ids");

    // --- the override chain ------------------------------------------------
    {
        ActorSoundSet type;
        type.set(ActorAction::Death, "enemy.hollow.death");
        ActorSoundSet placed;
        placed.set(ActorAction::Death, "boss.death");

        std::string storage;
        check(resolveActorCue(&placed, &type, ActorKind::Enemy,
                              ActorAction::Death, storage) == "boss.death",
              "the placed entity's cue wins over its type's");
        check(resolveActorCue(nullptr, &type, ActorKind::Enemy,
                              ActorAction::Death,
                              storage) == "enemy.hollow.death",
              "the type's cue wins when the placement states none");
        check(resolveActorCue(nullptr, nullptr, ActorKind::Enemy,
                              ActorAction::Death, storage) == "enemy.death",
              "the convention is the floor");
        // An empty row is "not stated", never "silent" -- otherwise adding a
        // sound table to an entity would mute every action it did not fill in.
        ActorSoundSet blank;
        check(resolveActorCue(&blank, &type, ActorKind::Enemy,
                              ActorAction::Death,
                              storage) == "enemy.hollow.death",
              "an empty row defers rather than silencing");
        // The convention is built into caller-owned storage, so the returned
        // view has to stay valid after the call.
        const std::string_view convention = resolveActorCue(
            nullptr, nullptr, ActorKind::Npc, ActorAction::Hurt, storage);
        check(convention == "npc.hurt" && storage == "npc.hurt",
              "the convention is written into the caller's storage");
    }

    // --- archetype inheritance ---------------------------------------------
    {
        ActorSoundSet archetype;
        archetype.set(ActorAction::Footstep, "enemy.footstep");
        archetype.set(ActorAction::Death, "enemy.death");
        ActorSoundSet variant;
        variant.set(ActorAction::Death, "enemy.knight.death");

        variant.inheritFrom(archetype);
        check(variant.cue(ActorAction::Death) == "enemy.knight.death",
              "a variant keeps the row it overrode");
        check(variant.cue(ActorAction::Footstep) == "enemy.footstep",
              "a variant inherits the rows it left alone");
        check(variant.cue(ActorAction::Alert).empty(),
              "inheritance invents nothing");
    }

    {
        ActorSoundSet empty;
        check(empty.empty(), "a fresh table is empty");
        empty.set(ActorAction::Idle, "enemy.idle");
        check(!empty.empty(), "one authored row makes it non-empty");
        ActorSoundSet other;
        check(other != empty, "tables compare by content");
        other.set(ActorAction::Idle, "enemy.idle");
        check(other == empty, "equal content compares equal");
    }

    if (failures == 0)
        std::printf("ActorSoundsTests: ok\n");
    return failures == 0 ? 0 : 1;
}
