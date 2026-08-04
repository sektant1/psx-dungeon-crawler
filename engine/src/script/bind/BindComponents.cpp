#include "script/bind/Bindings.h"

#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/World.h>

#include <cctype>
#include <cstring>
#include <string>

namespace eng::script {
namespace {

// A component, as Lua holds it.
//
// {entity, type} and NEVER a component pointer. Every read and every write
// calls type->instance() again, because any emplace can move a pool and a
// cached pointer would then be writing into whatever moved in behind it. That
// is a documented invariant of this ECS, and ScriptBindingTests asserts it with
// 512 emplaces between two writes through the same proxy.
struct LuaComponent {
    ecs::World* world = nullptr;
    entt::entity e = entt::null;
    const ecs::ComponentType* type = nullptr;

    void* live() const
    {
        if (world == nullptr || type == nullptr || type->instance == nullptr)
            return nullptr;
        if (!world->registry().valid(e)) return nullptr;
        return type->instance(world->registry(), e);
    }
};

// Field names in the reflection table are the C++ member names, so Spin's is
// "degreesPerSecond". The rest of this Lua API is snake_case -- world_position,
// set_parent, fixed_update -- and one surface spelling names two ways is a
// papercut on every line a script author writes.
//
// So the lookup accepts both: exact first, then a comparison that ignores
// underscores and case. "degrees_per_second", "degreesPerSecond" and
// "Degrees_Per_Second" all find the same field, and nobody has to remember
// which side of the boundary they are on.
bool namesMatch(const char* fieldName, const char* luaName)
{
    const char* a = fieldName;
    const char* b = luaName;
    while (*a != '\0' && *b != '\0') {
        if (*a == '_') { ++a; continue; }
        if (*b == '_') { ++b; continue; }
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b)))
            return false;
        ++a;
        ++b;
    }
    while (*a == '_') ++a;
    while (*b == '_') ++b;
    return *a == '\0' && *b == '\0';
}

const ecs::Field* findField(const ecs::ComponentType& t, const char* name)
{
    for (int i = 0; i < t.fieldCount; ++i)
        if (t.fields[i].name != nullptr && std::strcmp(t.fields[i].name, name) == 0)
            return &t.fields[i];
    for (int i = 0; i < t.fieldCount; ++i)
        if (t.fields[i].name != nullptr && namesMatch(t.fields[i].name, name))
            return &t.fields[i];
    return nullptr;
}

sol::object readField(sol::state_view lua, const void* base, const ecs::Field& f)
{
    const void* p = ecs::fieldPtr(base, f);
    switch (f.type) {
    case ecs::FieldType::Bool:
        return sol::make_object(lua, *static_cast<const bool*>(p));
    case ecs::FieldType::Int:
        return sol::make_object(lua, *static_cast<const int*>(p));
    case ecs::FieldType::Float:
        return sol::make_object(lua, *static_cast<const float*>(p));
    case ecs::FieldType::Vec3:
    case ecs::FieldType::Colour:
        return sol::make_object(lua, *static_cast<const glm::vec3*>(p));
    case ecs::FieldType::Quat:
        // Deliberately not exposed. A script author wants Euler degrees, which
        // the entity handle's `rotation` already gives; a raw quaternion here
        // would be a footgun with no use case behind it.
        return sol::lua_nil;
    case ecs::FieldType::String:
        return sol::make_object(lua, *static_cast<const std::string*>(p));
    }
    return sol::lua_nil;
}

void writeField(void* base, const ecs::Field& f, const sol::object& v)
{
    void* p = ecs::fieldPtr(base, f);
    switch (f.type) {
    case ecs::FieldType::Bool:
        if (v.is<bool>()) *static_cast<bool*>(p) = v.as<bool>();
        break;
    case ecs::FieldType::Int:
        if (v.is<int>()) *static_cast<int*>(p) = v.as<int>();
        break;
    case ecs::FieldType::Float:
        if (v.is<float>()) *static_cast<float*>(p) = v.as<float>();
        break;
    case ecs::FieldType::Vec3:
    case ecs::FieldType::Colour:
        if (v.is<glm::vec3>()) *static_cast<glm::vec3*>(p) = v.as<glm::vec3>();
        break;
    case ecs::FieldType::Quat:
        break; // see readField
    case ecs::FieldType::String:
        if (v.is<std::string>())
            *static_cast<std::string*>(p) = v.as<std::string>();
        break;
    }
}

// The registry the accessors walk. A file-scope pointer rather than a capture
// because sol2 usertype entries are registered once per state and this process
// runs one host at a time in every real configuration; the tests construct
// several hosts in sequence, never concurrently.
const ecs::ComponentRegistry* gRegistry = nullptr;

const ecs::ComponentType* findType(const std::string& name)
{
    if (gRegistry == nullptr) return nullptr;
    for (const ecs::ComponentType& t : gRegistry->types())
        if (t.name != nullptr && name == t.name) return &t;
    return nullptr;
}

// Raises rather than returning nil: a misspelled component name is a bug in the
// script, and the only way the author finds out is if it says so.
const ecs::ComponentType& requireType(const std::string& name)
{
    const ecs::ComponentType* t = findType(name);
    if (t == nullptr)
        throw sol::error("no component named '" + name +
                         "' is registered -- check the spelling against the "
                         "add-component menu");
    return *t;
}

// Transform is excluded from the generic path on purpose. It has first-class
// accessors on the entity handle that route through World::setLocalTransform,
// and reaching it here would let a script write a field offset directly and
// bypass the dirty flag that makes the write visible.
bool isExcluded(const std::string& name) { return name == "Transform"; }

const ecs::ComponentType& requireGenericType(const std::string& name)
{
    if (isExcluded(name))
        throw sol::error("Transform is not reachable through get/set; use "
                         "entity.position, entity.rotation and entity.scale, "
                         "which mark the subtree dirty");
    return requireType(name);
}

} // namespace

void setComponentRegistry(const ecs::ComponentRegistry* reg) { gRegistry = reg; }

void bindComponents(sol::state& lua, sol::usertype<LuaEntity>& entity)
{
    lua.new_usertype<LuaComponent>(
        "Component", sol::no_constructor,

        sol::meta_function::index,
        [](const LuaComponent& c, const std::string& field,
           sol::this_state ts) -> sol::object {
            const void* base = c.live();
            if (base == nullptr || c.type == nullptr) return sol::lua_nil;
            const ecs::Field* f = findField(*c.type, field.c_str());
            if (f == nullptr) return sol::lua_nil;
            return readField(sol::state_view(ts), base, *f);
        },

        sol::meta_function::new_index,
        [](const LuaComponent& c, const std::string& field,
           const sol::object& value) {
            void* base = c.live();
            if (base == nullptr || c.type == nullptr) return;
            if (const ecs::Field* f = findField(*c.type, field.c_str()))
                writeField(base, *f, value);
        });

    entity["has"] = [](const LuaEntity& h, const std::string& name) {
        if (!h.valid()) return false;
        const ecs::ComponentType& t = requireGenericType(name);
        return t.has != nullptr && t.has(h.world->registry(), h.e);
    };

    entity["add"] = [](LuaEntity& h, const std::string& name) {
        if (!h.valid()) return;
        const ecs::ComponentType& t = requireGenericType(name);
        if (t.addDefault != nullptr) t.addDefault(h.world->registry(), h.e);
    };

    entity["remove"] = [](LuaEntity& h, const std::string& name) {
        if (!h.valid()) return;
        const ecs::ComponentType& t = requireGenericType(name);
        if (t.remove != nullptr && t.has != nullptr &&
            t.has(h.world->registry(), h.e))
            t.remove(h.world->registry(), h.e);
    };

    entity["get"] = [](const LuaEntity& h, const std::string& name,
                       sol::this_state ts) -> sol::object {
        if (!h.valid()) return sol::lua_nil;
        const ecs::ComponentType& t = requireGenericType(name);
        if (t.has == nullptr || !t.has(h.world->registry(), h.e))
            return sol::lua_nil;
        return sol::make_object(sol::state_view(ts),
                                LuaComponent{h.world, h.e, &t});
    };

    entity["set"] = [](LuaEntity& h, const std::string& name,
                       const sol::table& values) {
        if (!h.valid()) return;
        const ecs::ComponentType& t = requireGenericType(name);
        if (t.has != nullptr && !t.has(h.world->registry(), h.e) &&
            t.addDefault != nullptr)
            t.addDefault(h.world->registry(), h.e);
        // Resolved once here and not held across the loop: nothing between this
        // line and the end of the loop can emplace, because the loop only
        // writes fields of a component that already exists.
        void* base = t.instance != nullptr ? t.instance(h.world->registry(), h.e)
                                           : nullptr;
        if (base == nullptr) return;
        for (const auto& kv : values) {
            if (!kv.first.is<std::string>()) continue;
            const std::string key = kv.first.as<std::string>();
            if (const ecs::Field* f = findField(t, key.c_str()))
                writeField(base, *f, kv.second);
        }
    };
}

} // namespace eng::script
