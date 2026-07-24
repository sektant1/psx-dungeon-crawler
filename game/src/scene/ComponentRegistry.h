#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

namespace mapio {

class ByteWriter;
class ByteReader;

// One serializable/editable component type. Function pointers keep the table
// POD and free of virtual dispatch; the inspector hook is added in Plan 2.
struct ComponentType {
    const char* name = nullptr;
    uint16_t stableTypeId = 0; // persisted in .map, never reused

    void (*addDefault)(entt::registry&, entt::entity) = nullptr;
    bool (*has)(const entt::registry&, entt::entity) = nullptr;
    void (*remove)(entt::registry&, entt::entity) = nullptr;
    void (*serialize)(const entt::registry&, entt::entity, ByteWriter&) = nullptr;
    void (*deserialize)(entt::registry&, entt::entity, ByteReader&) = nullptr;
};

// Ordered list of component types. Serializer, inspector (Plan 2), and the
// add-component menu all iterate this.
class ComponentRegistry {
public:
    void add(const ComponentType& t) { mTypes.push_back(t); }
    const std::vector<ComponentType>& types() const { return mTypes; }
    const ComponentType* find(uint16_t stableTypeId) const;

private:
    std::vector<ComponentType> mTypes;
};

// The process-wide registry with all core + gameplay types registered once.
const ComponentRegistry& coreRegistry();

} // namespace mapio
