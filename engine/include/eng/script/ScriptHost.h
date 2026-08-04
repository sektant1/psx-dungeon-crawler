#pragma once
#include <eng/script/ScriptConfig.h>

#include <cstddef>
#include <memory>
#include <string>

namespace eng::ecs {
class World;
class ComponentRegistry;
}

namespace eng::script {

// Owns the Lua state, the loaded-chunk cache and the live script instances for
// one World.
//
// Not an eng::System. A System's contract is a single update(dt), and this
// host's whole point is that its callbacks land at three different places in
// the frame -- fixed_update before a physics step, contacts after it, update
// with the rest of presentation. A generic update(dt) would hide the one thing
// a reader needs to know about it.
//
// PIMPL'd because sol2 is template-heavy: <sol/sol.hpp> stays behind this
// header so including it does not cost every consumer a second of compile time.
class ScriptHost
{
public:
    // `world` and `registry` must both outlive the host. The registry is what
    // the reflection bindings walk, so an application that registers its own
    // components gets them in Lua for free -- which is the whole reason the
    // generic component path exists.
    ScriptHost(ecs::World& world, const ScriptConfig& config,
               const ecs::ComponentRegistry& registry);
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // --- frame -----------------------------------------------------------
    // Creates instances for entities whose Scripts have none, runs start() on
    // any that have not started, then update(dt) on the rest.
    //
    // Call once per frame after gameplay has mutated components and BEFORE
    // World::sync() -- the same slot tickComponentSystems() occupies.
    void tick(float dt);

    // --- test and tooling seams ------------------------------------------
    std::size_t instanceCount() const;
    bool luaGlobalBool(const char* name) const;
    double luaGlobalNumber(const char* name) const;
    std::string luaGlobalString(const char* name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng::script
