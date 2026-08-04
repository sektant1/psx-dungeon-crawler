#include "script/bind/Bindings.h"

#include <eng/Physics.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/BodyRef.h>

namespace eng::script {

entt::entity entityForBody(ecs::World& world, BodyHandle body)
{
    if (!body.valid()) return entt::null;
    // A scan of the BodyRef view rather than a maintained map: BodyRef is
    // written by PhysicsSync, so a map here would need invalidating on every
    // body create and destroy. This runs once per raycast and once per contact,
    // not once per body per frame.
    for (const entt::entity e : world.registry().view<ecs::BodyRef>())
        if (world.registry().get<ecs::BodyRef>(e).handle == body) return e;
    return entt::null;
}

void bindPhysics(sol::state& lua, Physics& physics, ecs::World& world)
{
    sol::table t = lua.create_named_table("physics");

    t["raycast"] = [&physics, &world](const glm::vec3& from, const glm::vec3& dir,
                                      float dist, sol::optional<uint32_t> mask,
                                      sol::this_state ts) -> sol::object {
        RayHit hit;
        const CollisionMask layers =
            mask.has_value() ? CollisionMask{*mask} : kAllLayers;
        if (!physics.rayCast(from, dir, dist, hit, layers)) return sol::lua_nil;

        sol::state_view lv(ts);
        sol::table out = lv.create_table();
        const entt::entity e = entityForBody(world, hit.body);
        // `entity` stays nil when the body has no entity behind it. The level's
        // batched static geometry is one body per region and not an entity at
        // all, and a script must be able to tell "I hit the world" from "I hit
        // a thing".
        if (e != entt::null) out["entity"] = LuaEntity{&world, e};
        out["point"] = hit.point;
        out["normal"] = hit.normal;
        out["fraction"] = hit.fraction;
        return out;
    };
}

} // namespace eng::script
