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

// importStaticModel, but through the conditioned pack when there is one.
//
// This is the seam the Asset Conditioning Pipeline plugs into. A `.rmesh` --
// either because `path` names one directly, or because a mounted pack has one
// for it -- is read straight off disk; anything else runs Assimp exactly as
// before. Callers do not change and cannot tell, which is the point: the
// pipeline had to be adoptable without touching the twelve places that load a
// mesh.
//
// The conditioned file's import settings come from the resource database and
// the caller's `options` do not apply to it. That is not a compromise, it is
// the design: which pivot and unit scale an asset uses is a property of the
// asset, and the pack recorded the answer. A call site that passes options
// which DISAGREE with the record gets the record's, and the mismatch is logged
// once, because two answers to "how big is this model" is a content bug.
bool loadStaticModel(const std::filesystem::path& path,
                     const ModelImportOptions& options, ImportedModelData& out,
                     ModelImportReport& report);

// Applies legacy arbitrary vertex baking after canonical import. Used by
// existing procedural call sites whose matrices cannot be represented as TRS.
bool transformImportedModel(ImportedModelData& model, const glm::mat4& transform,
                            std::string& error);
MeshBounds importedModelBounds(const ImportedModelData& model);

std::vector<std::string> supportedAssimpModelExtensions();
bool assimpSupportsModelFile(const std::filesystem::path& path);

} // namespace eng::detail
