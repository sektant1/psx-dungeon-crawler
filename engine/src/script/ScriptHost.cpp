#include <eng/script/ScriptHost.h>

#include "script/ScriptChunkCache.h"
#include "script/ScriptError.h"
#include "script/ScriptInstance.h"
#include "script/bind/Bindings.h"

#include <eng/Log.h>
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

        bindMath(lua);
        entityType = bindEntity(lua);
        setComponentRegistry(&registry);
        bindComponents(lua, entityType);

        WorldCallbacks cb;
        cb.queueDestroy = [this](entt::entity target, bool hierarchy) {
            if (tearingDown) {
                log::warn("Script: destroy refused during teardown");
                return;
            }
            pendingDestroy.push_back({target, hierarchy});
        };
        cb.sendEvent = [this](entt::entity target, const std::string& n,
                              sol::object d) { sendEvent(target, n, std::move(d)); };
        cb.broadcastEvent = [this](const std::string& n, sol::object d) {
            broadcastEvent(n, std::move(d));
        };
        bindWorld(lua, world, cb);

        // Registered here rather than in BindEntity because both reach into
        // the instance pool, which is the host's and not a binding's.
        entityType["send"] = [this](const LuaEntity& h, const std::string& name,
                                    sol::object data) {
            if (h.valid()) sendEvent(h.e, name, std::move(data));
        };
        entityType["script"] = [this](const LuaEntity& h,
                                      sol::optional<std::string> p) {
            if (!h.valid()) return sol::object(sol::lua_nil);
            return instanceTable(h.e, p.value_or(std::string{}));
        };

        // on_destroy fires from here rather than from World::destroy, because
        // the World must not know scripting exists. entt calls this while the
        // entity is still valid, so a script can read its own components one
        // last time.
        world.registry().on_destroy<ScriptState>().connect<&Impl::onStateDestroyed>(
            this);
    }

    ~Impl()
    {
        // The registry outlives this host in the editor's preview world, and a
        // dangling listener would call through freed memory on the next level
        // teardown.
        world.registry()
            .on_destroy<ScriptState>()
            .disconnect<&Impl::onStateDestroyed>(this);
        instances.clear();
        chunks.clear();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void onStateDestroyed(entt::registry& reg, entt::entity e)
    {
        const ScriptState& state = reg.get<ScriptState>(e);
        // Teardown: a script spawning from on_destroy would be spawning into a
        // registry that is mid-mutation. The flag is read by the world bindings,
        // which refuse and log instead.
        const bool wasTearingDown = tearingDown;
        tearingDown = true;
        for (const uint32_t slot : state.instances) {
            if (ScriptInstance* inst = instances.get(slot)) {
                call(*inst, "on_destroy");
                instances.release(slot);
            }
        }
        tearingDown = wasTearingDown;
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

    // --- structural changes, deferred -------------------------------------
    struct PendingDestroy {
        entt::entity e;
        bool hierarchy;
    };
    std::vector<PendingDestroy> pendingDestroy;

    void flushDestroys()
    {
        // Swapped out first: an on_destroy handler may itself queue a destroy,
        // and appending to the vector being iterated would invalidate it.
        std::vector<PendingDestroy> batch;
        batch.swap(pendingDestroy);
        for (const PendingDestroy& d : batch) {
            if (!world.registry().valid(d.e)) continue;
            if (d.hierarchy)
                world.destroyHierarchy(d.e);
            else
                world.destroy(d.e);
        }
    }

    // --- messaging --------------------------------------------------------
    void sendEvent(entt::entity target, const std::string& name, sol::object data)
    {
        auto& reg = world.registry();
        if (!reg.valid(target) || !reg.all_of<ScriptState>(target)) return;
        // Copied: on_event may attach a script, which reallocates the vector.
        const std::vector<uint32_t> slots = reg.get<ScriptState>(target).instances;
        for (const uint32_t slot : slots)
            if (ScriptInstance* inst = instances.get(slot))
                call(*inst, "on_event", name, data);
    }

    void broadcastEvent(const std::string& name, sol::object data)
    {
        for (const uint32_t slot : liveSlots())
            if (ScriptInstance* inst = instances.get(slot))
                call(*inst, "on_event", name, data);
    }

    // The first instance of `path` on `e`, or nil. Empty path means the first
    // instance of any script. Used by Entity:script().
    sol::object instanceTable(entt::entity e, const std::string& path)
    {
        auto& reg = world.registry();
        if (!reg.valid(e) || !reg.all_of<ScriptState>(e)) return sol::lua_nil;
        for (const uint32_t slot : reg.get<ScriptState>(e).instances) {
            const ScriptInstance* inst = instances.get(slot);
            if (inst != nullptr && (path.empty() || inst->path == path))
                return sol::object(inst->self);
        }
        return sol::lua_nil;
    }

    // --- authored props ---------------------------------------------------
    // Builds self.props for one script instance.
    sol::table buildProps(entt::entity owner, const ecs::ScriptRef& ref)
    {
        sol::table props = lua.create_table();
        for (const ecs::ScriptProp& p : ref.props) {
            switch (p.type) {
            case ecs::ScriptProp::Type::Bool:   props[p.key] = p.b; break;
            case ecs::ScriptProp::Type::Number: props[p.key] = p.n; break;
            case ecs::ScriptProp::Type::String: props[p.key] = p.s; break;
            case ecs::ScriptProp::Type::Vec3:   props[p.key] = p.v; break;
            case ecs::ScriptProp::Type::Entity: {
                const entt::entity target = findByName(world, p.s);
                if (target == entt::null) {
                    // A warning, not an error: a level may legitimately ship
                    // without the collaborator, and the cooker already fails
                    // the build on a name absent from the authored scene.
                    log::warn("Script: %s on %s: prop '%s' names entity '%s', "
                              "which does not exist",
                              ref.path.c_str(), subject(owner).c_str(),
                              p.key.c_str(), p.s.c_str());
                    props[p.key] = sol::lua_nil;
                } else {
                    props[p.key] = LuaEntity{&world, target};
                }
                break;
            }
            }
        }
        return props;
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
                self["entity"] = LuaEntity{&world, e};
                self["props"] = buildProps(e, ref);

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
    // Held so binders that need the host's internals can add methods to the
    // entity usertype after it is registered.
    sol::usertype<LuaEntity> entityType;
    // True while on_destroy handlers are running. Structural changes from Lua
    // are refused during that window.
    bool tearingDown = false;
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

    mImpl->flushDestroys();
}

void ScriptHost::bindInput(Input& input)
{
    eng::script::bindInput(mImpl->lua, input);
}

void ScriptHost::bindPhysics(Physics& physics)
{
    eng::script::bindPhysics(mImpl->lua, physics, mImpl->world);
}

void ScriptHost::broadcast(const std::string& name)
{
    mImpl->broadcastEvent(name, sol::lua_nil);
    mImpl->flushDestroys();
}

void ScriptHost::fixedTick(float dt)
{
    for (const uint32_t slot : mImpl->liveSlots()) {
        ScriptInstance* inst = mImpl->instances.get(slot);
        // Not started means start() has not run: a fixed step that beat the
        // first frame must not call fixed_update on an uninitialised self.
        if (!inst || !inst->started) continue;
        mImpl->call(*inst, "fixed_update", dt);
    }
    mImpl->flushDestroys();
}

std::size_t ScriptHost::revive()
{
    std::size_t revived = 0;
    mImpl->instances.forEach([&](uint32_t, ScriptInstance& inst) {
        if (inst.quarantined) {
            inst.quarantined = false;
            ++revived;
        }
    });
    return revived;
}

bool ScriptHost::isQuarantined(entt::entity e, const std::string& path) const
{
    const auto& reg = mImpl->world.registry();
    if (!reg.valid(e) || !reg.all_of<ScriptState>(e)) return false;
    for (const uint32_t slot : reg.get<ScriptState>(e).instances) {
        const ScriptInstance* inst = mImpl->instances.get(slot);
        if (inst && inst->path == path) return inst->quarantined;
    }
    return false;
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

bool ScriptHost::luaGlobalNil(const char* name) const
{
    const sol::object o = mImpl->lua[name];
    return !o.valid() || o.get_type() == sol::type::lua_nil;
}

void ScriptHost::luaSetGlobalNil(const char* name)
{
    mImpl->lua[name] = sol::lua_nil;
}

void ScriptHost::luaSetGlobalEntityArray(
    const char* name, const std::vector<entt::entity>& entities)
{
    sol::table array = mImpl->lua.create_table();
    for (std::size_t i = 0; i < entities.size(); ++i)
        array[i + 1] = LuaEntity{&mImpl->world, entities[i]}; // Lua is 1-based
    mImpl->lua[name] = array;
}

} // namespace eng::script
