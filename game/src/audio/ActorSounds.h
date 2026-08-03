#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// What an actor IS, and what it sounds like doing each thing it can do.
//
// Deliberately dependency-free -- no entt, no eng, no toml -- because three
// very different places need the same vocabulary: the editor's inspector (which
// offers a cue per action), the cooker (which writes the authored table into a
// .map), and the runtime (which plays them). A vocabulary that lived in only
// one of the three would be re-spelled in the other two, and a misspelling of
// "telegraph" is a sound that never plays with nothing pointing at the cause.
namespace game {

// Whether an entity is somebody rather than something.
//
// This is the question the sound table needs answered: a wall has no actions,
// so offering it fourteen cue fields is noise. It is also the question the
// game already answers three inconsistent ways -- a PlayerSpawn component, an
// EnemySpawn component, an "enemy." marker -- and never for an NPC at all.
enum class ActorKind : std::uint8_t { Player, Npc, Enemy };
inline constexpr std::size_t kActorKindCount = 3;

const char* actorKindName(ActorKind kind);
// The cue-id namespace a kind's conventional sounds live in: "enemy", "player".
const char* actorKindCuePrefix(ActorKind kind);
bool parseActorKind(std::string_view name, ActorKind& out);

// Every action an actor can take that the runtime turns into a sound.
//
// The list is closed on purpose: each entry has a place in the code that plays
// it (see the `hint`), so an author picking a cue here gets a sound rather than
// a field that is saved, cooked and read by nothing. Adding an action is one
// row in the table below plus one call site.
enum class ActorAction : std::uint8_t {
    Spawn,
    Idle,
    Alert,
    Footstep,
    Jump,
    Land,
    Telegraph,
    Attack,
    Impact,
    Hurt,
    Block,
    Dodge,
    Death,
    Interact,
    Count,
};

inline constexpr std::size_t kActorActionCount =
    static_cast<std::size_t>(ActorAction::Count);

// Bit per kind, so one row can say "enemies and NPCs, not the player".
inline constexpr std::uint8_t actorKindBit(ActorKind kind)
{
    return static_cast<std::uint8_t>(1u << static_cast<std::uint8_t>(kind));
}
inline constexpr std::uint8_t kAnyActor =
    actorKindBit(ActorKind::Player) | actorKindBit(ActorKind::Npc) |
    actorKindBit(ActorKind::Enemy);

struct ActorActionInfo {
    ActorAction action;
    const char* id;    // stable key in .scn and TOML -- "telegraph"
    const char* label; // inspector row
    const char* hint;  // WHEN the runtime plays it, in gameplay terms
    std::uint8_t kinds; // which kinds can perform it
};

const std::array<ActorActionInfo, kActorActionCount>& actorActions();
const ActorActionInfo& actorActionInfo(ActorAction action);
const ActorActionInfo* findActorAction(std::string_view id);
bool actorPerforms(ActorKind kind, ActorAction action);

// One cue id per action. Empty means "not overridden here", never "silent":
// silence is what you get when nothing further down the chain names a cue
// either, which keeps an unset field from muting a sound the type authored.
struct ActorSoundSet {
    std::array<std::string, kActorActionCount> cues;

    const std::string& cue(ActorAction action) const
    {
        return cues[static_cast<std::size_t>(action)];
    }
    void set(ActorAction action, std::string cue)
    {
        cues[static_cast<std::size_t>(action)] = std::move(cue);
    }
    bool empty() const;
    // Every cue in `other` that this set does not name. Archetype inheritance
    // in enemies.toml, and nothing else: an override is a whole cue id, so
    // there is no field-wise merging below this granularity.
    void inheritFrom(const ActorSoundSet& other);
};

bool operator==(const ActorSoundSet& a, const ActorSoundSet& b);
inline bool operator!=(const ActorSoundSet& a, const ActorSoundSet& b)
{
    return !(a == b);
}

// --- components --------------------------------------------------------
// Both live here rather than in GameComponents.h because they are carried on
// two different registries: the world's (an authored placement, put there by
// the cooker) and combat's (a live enemy, put there by EnemySystem). A header
// that pulled in eng::Physics for the sake of a Collider would make the combat
// side pay for a dependency it does not have.

// This entity is somebody: a player, an NPC, or an enemy.
//
// The distinction was implicit before -- a PlayerSpawn component, an EnemySpawn
// component, an "enemy." marker, and nothing at all for an NPC -- so no code
// could ask "is this an actor" without enumerating the three. Anything that
// applies to actors as a class needs one component to key off, and its sound
// table is the first such thing.
struct Actor {
    ActorKind kind = ActorKind::Npc;
};

// Per-entity overrides of the cues this actor's actions emit. Absent means the
// actor sounds like its type says it does; present means this placed one is
// different (the boss variant of a grunt, a mute statue, a scripted NPC).
struct ActorSounds {
    ActorSoundSet set;
};

// The cue id an actor's action falls back to when nothing overrides it:
// "<kind>.<action>", e.g. "enemy.telegraph" or "player.footstep". A convention
// rather than a table, so a new action is authorable the moment its row exists
// -- and a cue the catalog does not define is a documented no-op, not a crash.
std::string actorConventionCue(ActorKind kind, ActorAction action);

// What to play, in the order overrides win: this placed entity, then its type
// (an enemies.toml row), then the convention. Either pointer may be null.
//
// Returns a view into one of the arguments or into `conventionStorage`, which
// the caller owns for the duration of the emission -- the convention id has to
// be built, and building it into a temporary would dangle.
std::string_view resolveActorCue(const ActorSoundSet* entity,
                                 const ActorSoundSet* type, ActorKind kind,
                                 ActorAction action,
                                 std::string& conventionStorage);

} // namespace game
