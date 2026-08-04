#include "script/bind/Bindings.h"

#include <eng/ecs/World.h>
#include <eng/ecs/components/Name.h>
#include <eng/ecs/components/Transform.h>
#include <eng/ecs/components/WorldTransform.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <string>

namespace eng::script {

bool LuaEntity::valid() const
{
    return world != nullptr && world->registry().valid(e);
}

namespace {

// The local Transform, or a default when the entity is gone. A stale handle
// yields zeros rather than raising: a script probing an optional collaborator
// should get "nothing there", not an error it has to pcall.
ecs::Transform localOrDefault(const LuaEntity& h)
{
    if (!h.valid()) return ecs::Transform{};
    const auto* t = h.world->registry().try_get<ecs::Transform>(h.e);
    return t ? *t : ecs::Transform{};
}

// Writes one field of the local Transform through World::setLocalTransform --
// never by mutating the component directly. That call is what marks the subtree
// Dirty, and a write that skipped it would draw at the old pose until something
// unrelated happened to move the entity.
template <typename Fn>
void editLocal(LuaEntity& h, Fn&& edit)
{
    if (!h.valid()) return;
    ecs::Transform t = localOrDefault(h);
    edit(t);
    h.world->setLocalTransform(h.e, t);
}

} // namespace

sol::usertype<LuaEntity> bindEntity(sol::state& lua)
{
    return lua.new_usertype<LuaEntity>(
        "Entity",
        // No constructor. Entities come from world.spawn, world.find or
        // self.entity; letting Lua fabricate one would produce a handle to an
        // id nobody allocated.
        sol::no_constructor,

        "valid", sol::property(&LuaEntity::valid),

        "name", sol::property([](const LuaEntity& h) -> std::string {
            if (!h.valid()) return {};
            const auto* n = h.world->registry().try_get<ecs::Name>(h.e);
            return n ? n->value : std::string{};
        }),

        "position",
        sol::property([](const LuaEntity& h) { return localOrDefault(h).position; },
                      [](LuaEntity& h, const glm::vec3& v) {
                          editLocal(h, [&](ecs::Transform& t) { t.position = v; });
                      }),

        "scale",
        sol::property([](const LuaEntity& h) { return localOrDefault(h).scale; },
                      [](LuaEntity& h, const glm::vec3& v) {
                          editLocal(h, [&](ecs::Transform& t) { t.scale = v; });
                      }),

        // Euler degrees, not a quaternion. A script author writing a door or a
        // patrol wants "turn 90 degrees about Y"; a quaternion is the right
        // storage and the wrong authoring surface, and the conversion is two
        // glm calls.
        //
        // Reading back is lossy, and deliberately so rather than by oversight:
        // Euler triples are not unique, so 120 degrees of yaw reads back as
        // (180, 60, 180) -- the same orientation, spelled differently. A script
        // that accumulates by reading its own rotation each frame will drift;
        // one that keeps its angle on self and writes it does not. Unity's
        // eulerAngles carries exactly this caveat, and the alternative -- a
        // second authored Euler stored beside the quaternion -- would be two
        // sources of truth for one pose.
        "rotation",
        sol::property(
            [](const LuaEntity& h) {
                return glm::degrees(glm::eulerAngles(localOrDefault(h).rotation));
            },
            [](LuaEntity& h, const glm::vec3& deg) {
                editLocal(h, [&](ecs::Transform& t) {
                    t.rotation = glm::quat(glm::radians(deg));
                });
            }),

        // Read-only: WorldTransform is derived by the hierarchy resolve, so a
        // write here would be silently overwritten on the next update.
        // Refusing loudly is the only honest option.
        "world_position",
        sol::property(
            [](const LuaEntity& h) {
                if (!h.valid()) return glm::vec3(0.0f);
                const auto* wt =
                    h.world->registry().try_get<ecs::WorldTransform>(h.e);
                // WorldTransform is the composed matrix; its translation is the
                // fourth column. Falls back to the local position for an entity
                // whose subtree has not been resolved yet, which is the honest
                // answer for a root.
                return wt ? glm::vec3(wt->matrix[3]) : localOrDefault(h).position;
            },
            [](LuaEntity&, const sol::object&) {
                throw sol::error("world_position is derived and read-only; "
                                 "set position instead");
            }),

        "set_parent", [](LuaEntity& h, const LuaEntity& parent) {
            if (!h.valid()) return;
            h.world->setParent(h.e, parent.valid() ? parent.e : entt::null);
        });
}

} // namespace eng::script
