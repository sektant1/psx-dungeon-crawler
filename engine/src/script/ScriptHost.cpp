#include <eng/script/ScriptHost.h>

#include <eng/ecs/World.h>

#include <sol/sol.hpp>

namespace eng::script {

struct ScriptHost::Impl {
    Impl(ecs::World& w, const ScriptConfig& c) : world(w), config(c)
    {
        // Deliberately not open_libraries() with no arguments: that opens io,
        // os and package, which let a gameplay script read the filesystem and
        // load native modules. A level script has no business doing either.
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                           sol::lib::table, sol::lib::debug);
    }

    ecs::World& world;
    ScriptConfig config;
    sol::state lua;
};

ScriptHost::ScriptHost(ecs::World& world, const ScriptConfig& config)
    : mImpl(std::make_unique<Impl>(world, config))
{
}

ScriptHost::~ScriptHost() = default;

} // namespace eng::script
