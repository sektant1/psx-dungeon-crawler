#pragma once

#include <eng/Primitive.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace eng::detail {

struct PrimitiveVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 colour{1.0f};
};

struct PrimitiveGeometry {
    std::vector<PrimitiveVertex> vertices;
    std::vector<uint32_t> indices;
};

// Pure CPU geometry seam shared by Renderer and headless tests.
std::optional<PrimitiveGeometry>
buildPrimitiveGeometry(const PrimitiveMeshDesc&);

} // namespace eng::detail
