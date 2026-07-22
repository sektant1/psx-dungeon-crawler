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

struct SpriteHandle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

struct ParticlesHandle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

// Index of a registered effect template (0 = invalid).
struct ParticleEffectId {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

// The scene root, valid after Engine::init.
inline constexpr NodeHandle kRootNode{1};

struct BodyHandle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
    bool operator==(const BodyHandle& o) const { return id == o.id; }
    bool operator!=(const BodyHandle& o) const { return id != o.id; }
};

struct CharacterHandle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

} // namespace eng
