#include "script/bind/Bindings.h"

#include <eng/Log.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Name.h>

#include <string>

namespace eng::script {

entt::entity findByName(ecs::World& world, const std::string& name)
{
    // A linear scan of the Name view, deliberately not an index: a name is
    // neither unique nor immutable, so a cache would need invalidating on every
    // Name write. The documented guidance is to resolve once in start() and
    // keep the handle on self -- which Entity props already do for you.
    for (const entt::entity e : world.registry().view<ecs::Name>())
        if (world.registry().get<ecs::Name>(e).value == name) return e;
    return entt::null;
}

void bindWorld(sol::state& lua, ecs::World& world, const WorldCallbacks& cb)
{
    sol::table w = lua.create_named_table("world");

    w["spawn"] = [&world](const std::string& name) {
        return LuaEntity{&world, world.create(name)};
    };

    w["find"] = [&world](const std::string& name,
                         sol::this_state ts) -> sol::object {
        const entt::entity e = findByName(world, name);
        if (e == entt::null) return sol::lua_nil;
        return sol::make_object(sol::state_view(ts), LuaEntity{&world, e});
    };

    // Queued, not immediate. A script calling destroy from inside update is
    // inside the host's own dispatch loop, and destroying there would
    // invalidate the views that loop is walking. The queue is flushed after
    // dispatch.
    w["destroy"] = [cb](const LuaEntity& h) {
        if (h.valid()) cb.queueDestroy(h.e, false);
    };
    w["destroy_hierarchy"] = [cb](const LuaEntity& h) {
        if (h.valid()) cb.queueDestroy(h.e, true);
    };

    sol::table l = lua.create_named_table("log");
    // The "Script:" prefix is what files these under the DebugConsole's
    // `Script` category -- the console derives a category from a leading
    // Word: prefix.
    l["info"] = [](const std::string& m) { log::info("Script: %s", m.c_str()); };
    l["warn"] = [](const std::string& m) { log::warn("Script: %s", m.c_str()); };
    l["error"] = [](const std::string& m) { log::error("Script: %s", m.c_str()); };

    sol::table ev = lua.create_named_table("event");
    ev["send"] = [cb](const LuaEntity& h, const std::string& name,
                      sol::object data) {
        if (h.valid()) cb.sendEvent(h.e, name, std::move(data));
    };
    ev["broadcast"] = [cb](const std::string& name, sol::object data) {
        cb.broadcastEvent(name, std::move(data));
    };
}

} // namespace eng::script
