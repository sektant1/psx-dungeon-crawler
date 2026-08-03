#pragma once

#include <eng/render/ModelImport.h>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace eng::detail {

struct ImportedSkinnedVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 texcoord{0.0f};
    glm::vec4 colour{1.0f};
    std::array<uint16_t, 4> joints{};
    std::array<float, 4> weights{1.0f, 0.0f, 0.0f, 0.0f};
};

struct ImportedSkinnedSubmesh {
    std::string name;
    std::string sourceMaterial;
    std::vector<ImportedSkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    // Indexed in ozz skeleton order. Unused joints remain identity.
    std::vector<glm::mat4> inverseBindPoses;
};

struct ImportedSkinnedModel {
    std::vector<ImportedSkinnedSubmesh> submeshes;
    MeshBounds bounds;
};

bool importSkinnedModel(const std::filesystem::path& path,
                        const std::vector<std::string>& skeletonJointNames,
                        ImportedSkinnedModel& out, std::string& error);

} // namespace eng::detail
