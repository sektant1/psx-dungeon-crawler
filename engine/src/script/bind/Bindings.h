#pragma once
#include <eng/script/ScriptHost.h>
#include <sol/sol.hpp>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <functional>
#include <string>

namespace eng {
class Audio;
class Input;
class Physics;
struct BodyHandle;
}

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

// What the world bindings need from the host, without this file having to see
// ScriptHost::Impl. std::function rather than a pointer to Impl: this header is
// the boundary between "what Lua may ask for" and "how the host does it", and
// neither side should be able to reach into the other.
struct WorldCallbacks {
    std::function<void(entt::entity, bool hierarchy)> queueDestroy;
    std::function<void(entt::entity, const std::string&, sol::object)> sendEvent;
    std::function<void(const std::string&, sol::object)> broadcastEvent;
};

// Linear scan of the Name view. Shared with the host, which resolves Entity
// props with it.
entt::entity findByName(ecs::World& world, const std::string& name);

void bindWorld(sol::state& lua, ecs::World& world, const WorldCallbacks& cb);

// --- optional subsystems ---------------------------------------------------
// A host given none of these still runs every script that only touches the
// World, which is what makes the headless tests real and lets a combat sim run
// scripted behaviour with no window.
void bindInput(sol::state& lua, Input& input);
void bindPhysics(sol::state& lua, Physics& physics, ecs::World& world);
void bindAudio(sol::state& lua, Audio& audio);

// RuntimeHooks is declared in the public header, because it is a parameter of
// ScriptHost::bindRuntime -- the runtime that fills it in lives above this
// library and cannot see this file.
void bindRuntime(sol::state& lua, const RuntimeHooks& hooks);

// save.get/set/commit, persisted to `path`.
//
// Deliberately a flat key -> string|number|bool table rather than a document
// store: what a game needs on day one is "which checkpoint, how much gold, is
// this door open", and a format somebody can read in a text editor is worth
// more here than one that can hold anything.
void bindSave(sol::state& lua, const std::string& path);

// timer.after/every/cancel over the host's TimerSet, which is ticked with the
// game clock so pause and slow-motion reach every scheduled callback at once.
class TimerSet;
void bindTimers(sol::state& lua, TimerSet& timers);

// Which entity owns a physics body, or null. Shared with the contact bridge.
entt::entity entityForBody(ecs::World& world, BodyHandle body);

} // namespace eng::script
