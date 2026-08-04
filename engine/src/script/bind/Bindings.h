#pragma once
#include <sol/sol.hpp>

#include <entt/entt.hpp>

namespace eng::ecs {
class World;
class ComponentRegistry;
}

namespace eng::script {

// An entity, as Lua holds it.
//
// A World pointer and an id -- never a component pointer, and never a bare id:
//   - a component pointer dies on the next emplace, because any emplace can
//     move a pool;
//   - a bare id cannot be validated, so a script holding one across a destroy
//     would read a recycled entity's components and act on the wrong object.
// Every accessor re-checks registry().valid() before it touches anything.
struct LuaEntity {
    ecs::World* world = nullptr;
    entt::entity e = entt::null;

    bool valid() const;
};

void bindMath(sol::state& lua);

// Returns the usertype so later binders can add methods to it -- the component
// accessors and the messaging calls need the host's internals and so cannot be
// registered from here.
sol::usertype<LuaEntity> bindEntity(sol::state& lua);

// The registry the generic component accessors walk. Must be set before
// bindComponents and must outlive the state.
void setComponentRegistry(const ecs::ComponentRegistry* reg);

// entity:get/set/has/add/remove, driven entirely by the registry's field table.
// This is what makes a component registered later scriptable with no Lua-side
// work, which is the property the engine's component table already prizes.
void bindComponents(sol::state& lua, sol::usertype<LuaEntity>& entity);

} // namespace eng::script
