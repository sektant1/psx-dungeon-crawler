#pragma once
#include <eng/script/ScriptHost.h>

#include <entt/entt.hpp>

#include <functional>

namespace game { namespace rpg { class RpgRuntime; } }

// This game's own vocabulary, published to Lua.
//
// WHY THESE ARE MODULES AND NOT COMPONENTS
// Lua reaches any registered component generically (`e:get("Spin")`), and that
// is the right mechanism for anything living on a scene entity. It cannot be
// the mechanism here, for one structural reason: combat runs on
// `CombatDirector`'s *own* registry, not the World the script host binds. An
// enemy's Health is not on the entity a script can name, so no amount of
// registering combat components would let `e:get("Health")` find one.
//
// Bridging the two registries was the alternative, and it is the wrong trade: it
// would mean a permanent entity-to-entity map maintained on every spawn and
// death, to expose fields that scripts overwhelmingly want to *read* about the
// player specifically. A narrow typed surface says what is actually on offer,
// and the map can still be built later without changing a line of Lua.
//
// The RPG half has no such excuse -- it simply has no entities at all. Quests,
// standing, currency and the raid phase are one runtime object, and a function
// is the honest shape for reaching them.
namespace game {

struct ScriptGameplayDeps {
    rpg::RpgRuntime* rpg = nullptr;
    // The combat registry. May be absent in a headless host, in which case the
    // player module reports zeros rather than being missing -- a script that
    // asks for health during a test should get an answer, not a nil call.
    entt::registry* combat = nullptr;
    // Who the player *currently* is, asked per call rather than captured.
    //
    // A value here would be read once, at bind time, and the modules are bound
    // before the combat model is wired -- so it would be entt::null forever and
    // every pool would read zero on a living player. It also has to survive a
    // level transition, which builds a new combatant: the entity is not stable
    // and nothing that caches it stays correct.
    std::function<entt::entity()> player;
};

// `player.*`: the pools and where they are. Read-mostly by design -- a script
// that wants to hurt somebody goes through `player.damage`, which routes into
// the combat system rather than writing the field, so i-frames, resistances and
// death still mean what they mean.
eng::script::ScriptModule playerModule(const ScriptGameplayDeps&);

// `rpg.*`: items, currency, flags, standing, quests, and the raid loop.
eng::script::ScriptModule rpgModule(const ScriptGameplayDeps&);

} // namespace game
