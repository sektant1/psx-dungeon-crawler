#pragma once
#include <cstdint>

namespace eng {

struct MeshHandle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

struct NodeHandle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

struct LightHandle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

struct StaticBatchHandle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

// The scene root, valid after Engine::init.
inline constexpr NodeHandle kRootNode{1};

} // namespace eng
