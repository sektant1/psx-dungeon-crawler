#include "ScriptGameplay.h"

#include "combat/CombatComponents.h"
#include "combat/FeelComponents.h"
#include "rpg/RpgRuntime.h"

#include <eng/Log.h>

#include <algorithm>
#include <string>

namespace game {
namespace {

using eng::script::ScriptArgs;
using eng::script::ScriptModule;
using eng::script::ScriptValue;

// The pools, by the name a script says. A table rather than an if-chain so the
// set of readable stats is one list -- which is also what `player.stats()` would
// enumerate if anything ever needs to.
//
// Every getter takes the registry and entity and answers 0 when the component
// is absent, which is the same answer an entity with no such pool should give:
// a script asking a corpse for its stamina is not an error.
struct PoolReader {
    const char* name;
    float (*read)(const entt::registry&, entt::entity);
};

template <typename T, float T::*Member>
float poolOf(const entt::registry& r, entt::entity e)
{
    const T* c = r.valid(e) ? r.try_get<T>(e) : nullptr;
    return c ? c->*Member : 0.0f;
}

// The player's component, or null. One helper because every accessor needs the
// same three guards -- a registry, a provider, and an entity the registry still
// considers valid -- and forgetting the third is a read of a recycled slot.
template <typename T>
T* livePlayer(entt::registry* reg, const std::function<entt::entity()>& who)
{
    if (!reg || !who)
        return nullptr;
    const entt::entity e = who();
    return reg->valid(e) ? reg->try_get<T>(e) : nullptr;
}

const PoolReader kPools[] = {
    {"health",      &poolOf<Health, &Health::current>},
    {"health_max",  &poolOf<Health, &Health::max>},
    {"stamina",     &poolOf<Stamina, &Stamina::current>},
    {"stamina_max", &poolOf<Stamina, &Stamina::max>},
    {"mana",        &poolOf<Mana, &Mana::current>},
    {"mana_max",    &poolOf<Mana, &Mana::max>},
    {"poise",       &poolOf<Poise, &Poise::current>},
    {"poise_max",   &poolOf<Poise, &Poise::max>},
};

} // namespace

ScriptModule playerModule(const ScriptGameplayDeps& deps)
{
    ScriptModule module;
    module.table = "player";
    entt::registry* combat = deps.combat;
    // Copied, not dereferenced: the module outlives this call and the provider
    // is what keeps it pointed at the live combatant across a level change.
    const std::function<entt::entity()> playerOf = deps.player;

    module.functions.emplace_back(
        "stat", [combat, playerOf](const ScriptArgs& args) -> ScriptValue {
            const std::string which = eng::script::argString(args, 0);
            if (!combat)
                return 0.0;
            for (const PoolReader& pool : kPools)
                if (which == pool.name)
                    return double(pool.read(*combat, playerOf ? playerOf() : entt::entity(entt::null)));
            eng::log::warn("Script: player.stat('%s') is not a pool",
                           which.c_str());
            return 0.0;
        });

    // A convenience over stat(), because "am I nearly dead" is the single most
    // common thing a scripted encounter asks and `player.stat("health") /
    // player.stat("health_max")` gets the divide-by-zero wrong eventually.
    module.functions.emplace_back(
        "health_fraction", [combat, playerOf](const ScriptArgs&) -> ScriptValue {
            const Health* h =
                livePlayer<Health>(combat, playerOf);
            if (!h || h->max <= 0.0f)
                return 0.0;
            return double(std::clamp(h->current / h->max, 0.0f, 1.0f));
        });

    module.functions.emplace_back(
        "alive", [combat, playerOf](const ScriptArgs&) -> ScriptValue {
            const Health* h =
                livePlayer<Health>(combat, playerOf);
            return h ? !h->dead() : false;
        });

    // Healing is a direct write because there is nothing to arbitrate: no
    // resistance applies, no i-frame blocks it, and clamping to max is the
    // whole rule. Damage is deliberately NOT its mirror image -- see below.
    module.functions.emplace_back(
        "heal", [combat, playerOf](const ScriptArgs& args) -> ScriptValue {
            Health* h = livePlayer<Health>(combat, playerOf);
            if (!h)
                return 0.0;
            const float before = h->current;
            h->current = std::min(h->max,
                                  h->current + float(std::max(
                                      0.0, eng::script::argNumber(args, 0))));
            return double(h->current - before);
        });

    // Bypasses resistances, i-frames and the death path on purpose, and says so:
    // this is the scripted-hazard verb (a trap, a scripted execution), not a
    // second damage pipeline. Anything that should behave like a hit belongs in
    // the combat system, and a script that wants one fires a weapon instead.
    module.functions.emplace_back(
        "hurt", [combat, playerOf](const ScriptArgs& args) -> ScriptValue {
            Health* h = livePlayer<Health>(combat, playerOf);
            if (!h)
                return 0.0;
            const float before = h->current;
            h->current = std::max(0.0f,
                                  h->current - float(std::max(
                                      0.0, eng::script::argNumber(args, 0))));
            return double(before - h->current);
        });

    return module;
}

ScriptModule rpgModule(const ScriptGameplayDeps& deps)
{
    ScriptModule module;
    module.table = "rpg";
    rpg::RpgRuntime* r = deps.rpg;

    // Every function short-circuits on a null runtime rather than the module
    // being absent, for the reason RuntimeHooks gives: a script calling
    // rpg.count() in a headless test should get 0 and a warning, not die on a
    // nil global halfway through an unrelated assertion.
    const auto requireRuntime = [](rpg::RpgRuntime* rt, const char* what) {
        if (!rt)
            eng::log::warn("Script: rpg.%s has no runtime in this host", what);
        return rt != nullptr;
    };

    module.functions.emplace_back(
        "count", [r, requireRuntime](const ScriptArgs& args) -> ScriptValue {
            if (!requireRuntime(r, "count"))
                return 0.0;
            return double(
                r->inventory().backpack.count(eng::script::argString(args, 0)));
        });
    module.functions.emplace_back(
        "stashed", [r, requireRuntime](const ScriptArgs& args) -> ScriptValue {
            if (!requireRuntime(r, "stashed"))
                return 0.0;
            return double(
                r->inventory().stash.count(eng::script::argString(args, 0)));
        });
    // Returns how many actually went in: a full pack is a real outcome and a
    // script that ignores the difference will duplicate items the moment the
    // carry limit bites.
    module.functions.emplace_back(
        "give", [r, requireRuntime](const ScriptArgs& args) -> ScriptValue {
            if (!requireRuntime(r, "give"))
                return 0.0;
            int placed = 0;
            r->give(eng::script::argString(args, 0),
                    int(eng::script::argNumber(args, 1, 1.0)),
                    eng::script::argBool(args, 2, true), &placed);
            return double(placed);
        });
    module.functions.emplace_back(
        "take", [r, requireRuntime](const ScriptArgs& args) -> ScriptValue {
            if (!requireRuntime(r, "take"))
                return 0.0;
            return double(r->takeAway(eng::script::argString(args, 0),
                                      int(eng::script::argNumber(args, 1, 1.0))));
        });
    module.functions.emplace_back(
        "currency", [r, requireRuntime](const ScriptArgs&) -> ScriptValue {
            if (!requireRuntime(r, "currency"))
                return 0.0;
            return double(r->inventory().currency);
        });

    module.functions.emplace_back(
        "flag", [r, requireRuntime](const ScriptArgs& args) -> ScriptValue {
            if (!requireRuntime(r, "flag"))
                return false;
            return r->world().flag(eng::script::argString(args, 0));
        });
    // Routed through apply(Effect) rather than WorldState::setFlag so the flag
    // channel fires: a quest objective watching for it must hear about a flag a
    // script set exactly as it hears about one a dialogue choice set.
    module.functions.emplace_back(
        "set_flag", [r, requireRuntime](const ScriptArgs& args) -> ScriptValue {
            if (!requireRuntime(r, "set_flag"))
                return std::monostate{};
            rpg::Effect effect;
            effect.kind = eng::script::argBool(args, 1, true)
                              ? rpg::EffectKind::SetFlag
                              : rpg::EffectKind::ClearFlag;
            effect.subject = eng::script::argString(args, 0);
            r->apply(effect);
            return std::monostate{};
        });
    module.functions.emplace_back(
        "standing", [r, requireRuntime](const ScriptArgs& args) -> ScriptValue {
            if (!requireRuntime(r, "standing"))
                return 0.0;
            return double(r->world().standing(eng::script::argString(args, 0)));
        });
    module.functions.emplace_back(
        "quest_state", [r, requireRuntime](const ScriptArgs& args) -> ScriptValue {
            if (!requireRuntime(r, "quest_state"))
                return std::string("pending");
            return std::string(
                rpg::nameOf(r->quests().stateOf(eng::script::argString(args, 0))));
        });
    module.functions.emplace_back(
        "start_quest", [r, requireRuntime](const ScriptArgs& args) -> ScriptValue {
            if (!requireRuntime(r, "start_quest"))
                return false;
            rpg::Effect effect;
            effect.kind = rpg::EffectKind::StartQuest;
            effect.subject = eng::script::argString(args, 0);
            r->apply(effect);
            return r->quests().stateOf(effect.subject) != rpg::QuestState::Pending;
        });

    // The raid loop, read-only. A script may observe how deep it is and whether
    // the run is settled; it may not extract or kill the player, because both
    // are transactions that write the profile and belong to the app that owns
    // the frame loop.
    module.functions.emplace_back(
        "depth", [r, requireRuntime](const ScriptArgs&) -> ScriptValue {
            if (!requireRuntime(r, "depth"))
                return 0.0;
            return double(r->raid().depth());
        });
    module.functions.emplace_back(
        "phase", [r, requireRuntime](const ScriptArgs&) -> ScriptValue {
            if (!requireRuntime(r, "phase"))
                return std::string("safehouse");
            return std::string(rpg::nameOf(r->raid().phase()));
        });
    // What a death would cost right now, in coin. The one number that makes the
    // push-or-extract decision legible to a scripted prompt.
    module.functions.emplace_back(
        "loss_at_risk", [r, requireRuntime](const ScriptArgs&) -> ScriptValue {
            if (!requireRuntime(r, "loss_at_risk"))
                return 0.0;
            return double(r->previewLoss().lostValue());
        });

    module.functions.emplace_back(
        "note", [r, requireRuntime](const ScriptArgs& args) -> ScriptValue {
            if (!requireRuntime(r, "note"))
                return std::monostate{};
            r->note(eng::script::argString(args, 0));
            return std::monostate{};
        });

    return module;
}

} // namespace game
