#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

namespace editor {

struct InspectorEntry {
    uint16_t stableTypeId = 0;
    const char* label = nullptr;
    bool (*draw)(entt::registry&, entt::entity) = nullptr;
};

class InspectorRegistry {
public:
    void add(const InspectorEntry& e) { mEntries.push_back(e); }
    const std::vector<InspectorEntry>& entries() const { return mEntries; }
    const InspectorEntry* find(uint16_t stableTypeId) const;

private:
    std::vector<InspectorEntry> mEntries;
};

const InspectorRegistry& inspectorRegistry();

} // namespace editor
