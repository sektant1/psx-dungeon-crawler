#pragma once
#include <eng/script/ScriptConfig.h>

#include <memory>

namespace eng::ecs { class World; }

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
    ScriptHost(ecs::World& world, const ScriptConfig& config);
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng::script
