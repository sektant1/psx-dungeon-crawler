#include "script/ScriptContactBridge.h"

#include "script/bind/Bindings.h"

#include <eng/Physics.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Collider.h>

namespace eng::script {

ScriptContactBridge::ScriptContactBridge(Physics& physics, ecs::World& world)
    : mPhysics(physics), mWorld(world)
{
    mToken = mPhysics.addContactCallback([this](const HitEvent& ev) {
        const entt::entity a = entityForBody(mWorld, ev.self);
        const entt::entity b = entityForBody(mWorld, ev.other);
        // Neither side an entity means the level's batched static geometry
        // touching itself: nothing scripted can be listening.
        if (a == entt::null && b == entt::null) return;

        auto push = [&](entt::entity self, entt::entity other) {
            if (self == entt::null) return;
            const auto* col = mWorld.registry().try_get<ecs::Collider>(self);
            mQueue.push_back({self, other, ev.point, ev.normal, ev.impulse,
                              col != nullptr && col->sensor});
        };
        // Both directions. Each side hears about the other, which is what lets
        // a trigger volume react to the player without the player's script
        // having to know the volume exists.
        push(a, b);
        push(b, a);
    });
}

ScriptContactBridge::~ScriptContactBridge()
{
    // Before the host's Lua state dies. A live subscription calling into a
    // freed queue is the failure this destructor exists to prevent.
    if (mToken != 0) mPhysics.removeContactCallback(mToken);
}

std::vector<ScriptContact> ScriptContactBridge::drain()
{
    std::vector<ScriptContact> out;
    out.swap(mQueue);
    return out;
}

} // namespace eng::script
