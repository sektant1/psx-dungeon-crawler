#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

namespace eng::io {
class ByteWriter;
class ByteReader;
}

namespace eng::ecs {

// One serialisable/editable component type. Function pointers keep the table
// POD and free of virtual dispatch, so a registry is cheap to copy and can be
// built per world rather than being a process-wide singleton.
struct ComponentType {
    const char* name = nullptr;
    // Persisted in scene files, never reused. The engine reserves ids below
    // kFirstApplicationTypeId for its own components so an application can add
    // types without colliding with a future engine one.
    uint16_t stableTypeId = 0;

    void (*addDefault)(entt::registry&, entt::entity) = nullptr;
    bool (*has)(const entt::registry&, entt::entity) = nullptr;
    void (*remove)(entt::registry&, entt::entity) = nullptr;
    void (*serialize)(const entt::registry&, entt::entity, io::ByteWriter&) = nullptr;
    void (*deserialize)(entt::registry&, entt::entity, io::ByteReader&) = nullptr;
};

inline constexpr uint16_t kFirstApplicationTypeId = 10;

// Ordered list of component types. Serialisers, inspectors and add-component
// menus all iterate this.
class ComponentRegistry {
public:
    void add(const ComponentType& t) { mTypes.push_back(t); }
    const std::vector<ComponentType>& types() const { return mTypes; }
    const ComponentType* find(uint16_t stableTypeId) const;

private:
    std::vector<ComponentType> mTypes;
};

// Appends the engine's own components -- Name, Transform, MeshRenderer,
// LightRef -- with their reserved ids. Applications call this, then add their
// own types from kFirstApplicationTypeId up.
void registerEngineComponents(ComponentRegistry&);

} // namespace eng::ecs
