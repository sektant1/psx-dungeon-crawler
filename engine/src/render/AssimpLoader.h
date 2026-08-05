#pragma once

#include <eng/content/MeshData.h>
#include <eng/render/ModelImport.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace eng::detail {

// The importer's output is eng::content::MeshData -- the same structs the
// .rmesh writer and reader use, so the cooked path and the Assimp path cannot
// drift apart. These aliases keep the older spelling at the ~30 existing call
// sites; new code should name the content types directly.
using ImportedModelVertex = content::MeshVertex;
using ImportedModelSubmesh = content::MeshSubmesh;
using ImportedModelData = content::MeshData;

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
