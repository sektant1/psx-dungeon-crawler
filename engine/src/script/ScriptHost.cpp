#include <eng/script/ScriptHost.h>

#include "script/ScriptChunkCache.h"
#include "script/ScriptError.h"
#include "script/ScriptInstance.h"

#include <eng/ecs/World.h>
#include <eng/ecs/components/Name.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/script/ScriptState.h>

#include <sol/sol.hpp>

#include <utility>
#include <vector>

namespace eng::script {

struct ScriptHost::Impl {
    Impl(ecs::World& w, const ScriptConfig& c, const ecs::ComponentRegistry& reg)
        : world(w), config(c), registry(reg), chunks(lua)
    {
        // Deliberately not open_libraries() with no arguments: that opens io,
        // os and package, which let a gameplay script read the filesystem and
        // load native modules. A level script has no business doing either.
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                           sol::lib::table, sol::lib::debug);
        installTracebackHandler(lua);
    }

    // How an entity reads in an error report. Built on demand: an error is
    // rare, and a name lookup per instance per frame is not.
    std::string subject(entt::entity e) const
    {
        const auto& reg = world.registry();
        std::string label = "entity ";
        if (reg.valid(e)) {
            const auto* name = reg.try_get<ecs::Name>(e);
            if (name && !name->value.empty()) label += "'" + name->value + "' ";
        }
        label += "#" + std::to_string(uint32_t(entt::to_integral(e)));
        return label;
    }

    // Calls one callback on one instance, protected, with a traceback. Returns
    // false when it failed -- in which case the instance has already been
    // quarantined and reported.
    template <typename... Args>
    bool call(ScriptInstance& inst, const char* name, Args&&... args)
    {
        if (inst.quarantined) return false;
        const sol::object fn = inst.self[name];
        if (!fn.valid() || fn.get_type() != sol::type::function)
            return true; // a script defines only the callbacks it needs

        const sol::protected_function pf(fn.as<sol::function>(),
                                         tracebackHandler(lua));
        const sol::protected_function_result r =
            pf(inst.self, std::forward<Args>(args)...);
        if (r.valid()) return true;

        const sol::error err = r;
        reportScriptError(inst.path, name, subject(inst.entity), err.what());
        // Quarantine rather than retry. The same traceback sixty times a second
        // buries every other line in the console, and a script that failed once
        // in update will fail again next frame for the same reason.
        inst.quarantined = true;
        return false;
    }

    // Builds instances for any entity carrying Scripts but no ScriptState.
    void instantiateNew()
    {
        auto& reg = world.registry();
        // Snapshot: emplacing ScriptState is a write to a pool the view would
        // otherwise be iterating.
        std::vector<entt::entity> pending;
        for (const entt::entity e : reg.view<ecs::Scripts>())
            if (!reg.all_of<ScriptState>(e)) pending.push_back(e);

        for (const entt::entity e : pending) {
            const ecs::Scripts& scripts = reg.get<ecs::Scripts>(e);
            ScriptState state;
            for (const ecs::ScriptRef& ref : scripts.items) {
                if (!ref.enabled) continue;
                sol::table* cls = chunks.classFor(ref.path);
                if (!cls) continue; // already reported

                // The instance is an empty table whose __index is the class.
                // Methods are shared; everything the script assigns to self is
                // this entity's alone. That is also what makes hot reload cheap
                // later: swap __index, keep the table.
                sol::table self = lua.create_table();
                sol::table mt = lua.create_table();
                mt["__index"] = *cls;
                self[sol::metatable_key] = mt;

                state.instances.push_back(
                    instances.create(e, ref.path, std::move(self)));
            }
            reg.emplace<ScriptState>(e, std::move(state));
        }
    }

    // Every live slot, copied. Callers dispatch Lua while walking this, and a
    // script may spawn or destroy entities from inside a callback.
    std::vector<uint32_t> liveSlots() const
    {
        std::vector<uint32_t> slots;
        slots.reserve(instances.liveCount());
        instances.forEach(
            [&](uint32_t slot, const ScriptInstance&) { slots.push_back(slot); });
        return slots;
    }

    ecs::World& world;
    ScriptConfig config;
    const ecs::ComponentRegistry& registry;
    sol::state lua;
    ScriptChunkCache chunks;
    ScriptInstancePool instances;
};

ScriptHost::ScriptHost(ecs::World& world, const ScriptConfig& config,
                       const ecs::ComponentRegistry& registry)
    : mImpl(std::make_unique<Impl>(world, config, registry))
{
}

ScriptHost::~ScriptHost() = default;

void ScriptHost::tick(float dt)
{
    mImpl->instantiateNew();

    const std::vector<uint32_t> slots = mImpl->liveSlots();

    // start() for every new instance before any update(), rather than per
    // instance: a script's start must be able to see the level as it was built,
    // and that is only true if no update has moved anything yet.
    for (const uint32_t slot : slots) {
        ScriptInstance* inst = mImpl->instances.get(slot);
        if (!inst || inst->started || inst->quarantined) continue;
        inst->started = true; // set first, so a start that throws is not retried
        mImpl->call(*inst, "start");
    }

    for (const uint32_t slot : slots) {
        ScriptInstance* inst = mImpl->instances.get(slot);
        if (!inst || !inst->started) continue;
        mImpl->call(*inst, "update", dt);
    }
}

std::size_t ScriptHost::instanceCount() const
{
    return mImpl->instances.liveCount();
}

bool ScriptHost::luaGlobalBool(const char* name) const
{
    const sol::object o = mImpl->lua[name];
    return o.valid() && o.is<bool>() && o.as<bool>();
}

double ScriptHost::luaGlobalNumber(const char* name) const
{
    const sol::object o = mImpl->lua[name];
    return (o.valid() && o.is<double>()) ? o.as<double>() : 0.0;
}

std::string ScriptHost::luaGlobalString(const char* name) const
{
    const sol::object o = mImpl->lua[name];
    return (o.valid() && o.is<std::string>()) ? o.as<std::string>()
                                              : std::string();
}

} // namespace eng::script
