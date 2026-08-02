#pragma once

#include <eng/render/ModelImport.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace eng::detail {

struct ImportedModelVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 texcoord{0.0f};
    glm::vec4 colour{1.0f};
};

struct ImportedModelSubmesh {
    std::string name;
    std::string sourceMaterial;
    // Base-colour texture as the source file names it: relative to the model,
    // absolute, or "*N" for an embedded one. Empty when the material has none.
    // Never opened here -- it is for converters deciding what to copy.
    std::string sourceTexture;
    std::vector<ImportedModelVertex> vertices;
    std::vector<uint32_t> indices;
};

struct ImportedModelData {
    std::vector<ImportedModelSubmesh> submeshes;
    std::vector<glm::vec3> collisionVertices;
    std::vector<uint32_t> collisionIndices;
};

bool importStaticModel(const std::filesystem::path& path,
                       const ModelImportOptions& options,
                       ImportedModelData& out, ModelImportReport& report);

// Applies legacy arbitrary vertex baking after canonical import. Used by
// existing procedural call sites whose matrices cannot be represented as TRS.
bool transformImportedModel(ImportedModelData& model, const glm::mat4& transform,
                            std::string& error);
MeshBounds importedModelBounds(const ImportedModelData& model);

std::vector<std::string> supportedAssimpModelExtensions();
bool assimpSupportsModelFile(const std::filesystem::path& path);

} // namespace eng::detail
