#pragma once
#include <eng/script/ScriptConfig.h>

#include <entt/entity/fwd.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace eng {
class Input;
class Physics;
}

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

    // --- optional subsystems ---------------------------------------------
    // Both references must outlive the host. A host given neither still runs
    // every script that only touches the World -- which is what makes the
    // headless tests real, and lets a combat sim run scripted behaviour with
    // no window and no physics world.
    void bindInput(Input& input);
    void bindPhysics(Physics& physics);

    // --- frame -----------------------------------------------------------
    // Creates instances for entities whose Scripts have none, runs start() on
    // any that have not started, then update(dt) on the rest.
    //
    // Call once per frame after gameplay has mutated components and BEFORE
    // World::sync() -- the same slot tickComponentSystems() occupies.
    void tick(float dt);

    // Runs fixed_update(dt) on every started instance.
    //
    // Defined as "immediately before a physics step", not "on the fixed clock".
    // That keeps the contract true in a mode with no fixed loop -- MapPlay steps
    // physics from onPresent -- where the caller simply calls this first.
    void fixedTick(float dt);

    // --- messaging -------------------------------------------------------
    // Delivers on_event(name, nil) to every live instance. The C++ side of what
    // Lua reaches through event.broadcast, for a game that wants to announce
    // something from native code -- a level transition, a boss phase.
    void broadcast(const std::string& name);

    // --- errors ----------------------------------------------------------
    // Un-quarantines every instance that errored. Returns how many. Called by
    // the console and by a successful hot reload.
    std::size_t revive();

    // Whether this entity's instance of `path` is currently quarantined.
    bool isQuarantined(entt::entity e, const std::string& path) const;

    // --- test and tooling seams ------------------------------------------
    std::size_t instanceCount() const;
    bool luaGlobalBool(const char* name) const;
    double luaGlobalNumber(const char* name) const;
    std::string luaGlobalString(const char* name) const;
    bool luaGlobalNil(const char* name) const;
    void luaSetGlobalNil(const char* name);
    // Publishes entity handles as a 1-based Lua array. Exists so a test can
    // hand the VM entities it built in C++ before world.spawn is bound.
    void luaSetGlobalEntityArray(const char* name,
                                 const std::vector<entt::entity>& entities);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng::script
