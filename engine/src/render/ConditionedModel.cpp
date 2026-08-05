// Where the runtime meets the Asset Conditioning Pipeline.
//
// One function, and it is the entire runtime-side cost of the pipeline: ask the
// mounted pack whether this model has been conditioned, read the .rmesh if it
// has, fall through to Assimp if it has not. Everything above it -- the
// renderer, the editor's preview, model_validate -- is unchanged.

#include "AssimpLoader.h"

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>
#include <eng/content/MeshAsset.h>

#include <mutex>
#include <string>
#include <unordered_set>

namespace fs = std::filesystem;

namespace eng::detail {
namespace {

// One warning per model, ever. A mismatch is worth saying and not worth saying
// sixty times a frame, and the set is small: it only ever holds the models that
// are actually wrong.
std::mutex gWarnedMutex;
std::unordered_set<std::string> gWarned;

void warnOnce(const std::string& key, const char* format, const char* a,
              const char* b)
{
    {
        std::lock_guard<std::mutex> lock(gWarnedMutex);
        if (!gWarned.insert(key).second)
            return;
    }
    log::warn(format, key.c_str(), a, b);
}

const char* pivotName(uint8_t pivot)
{
    switch (static_cast<PivotMode>(pivot)) {
    case PivotMode::Source:
        return "source";
    case PivotMode::BoundsCenter:
        return "bounds_center";
    case PivotMode::BottomCenter:
        return "bottom_center";
    case PivotMode::Custom:
        return "custom";
    }
    return "?";
}

// Fills in what the .rmesh recorded, so a caller reading the report cannot tell
// which path produced the mesh. Anything the conditioned form does not carry --
// the per-import cleanup counts, the source material list -- stays zero and
// empty, which is honest: nothing was cleaned up on this run.
ModelImportReport reportFrom(const content::MeshAssetInfo& info,
                             const content::MeshData& mesh,
                             const fs::path& path)
{
    ModelImportReport report;
    report.sourcePath = info.sourcePath.empty() ? path.string() : info.sourcePath;
    report.format = info.format;
    report.importer = "Asset Conditioning Pipeline";
    report.sourceBytes = info.sourceBytes;
    report.vertices = mesh.vertexCount();
    report.triangles = mesh.triangleCount();
    report.sourceMeshes = info.sourceMeshes;
    report.submeshes = static_cast<uint32_t>(mesh.submeshes.size());
    report.materials = info.materials;
    report.appliedPivot = static_cast<PivotMode>(info.pivot);
    report.appliedMetresPerSourceUnit = info.metresPerSourceUnit;
    report.finalBounds = {info.boundsMin, info.boundsMax};
    report.sourceBounds = report.finalBounds;
    report.canonicalPivotStandard = info.canonicalPivotStandard;
    for (const content::MeshSubmesh& submesh : mesh.submeshes)
        report.sourceMaterials.push_back(submesh.sourceMaterial);
    return report;
}

// Geometry is baked. A conditioned mesh is therefore only usable by a caller
// that asked for exactly the settings it was built with -- anything else and
// the fast path would silently hand back a differently-shaped world.
bool settingsMatch(const content::MeshAssetInfo& info,
                   const ModelImportOptions& requested)
{
    const glm::quat orientation(info.sourceOrientation.x, info.sourceOrientation.y,
                                info.sourceOrientation.z, info.sourceOrientation.w);
    return info.pivot == static_cast<uint8_t>(requested.pivot) &&
           info.texcoordV == static_cast<uint8_t>(requested.texcoordV) &&
           info.metresPerSourceUnit == requested.metresPerSourceUnit &&
           orientation == requested.sourceOrientation &&
           info.customPivot == requested.customPivot;
}

bool readConditioned(const fs::path& file, const ModelImportOptions& options,
                     ImportedModelData& out, ModelImportReport& report,
                     bool allowFallback)
{
    content::MeshAssetInfo info;
    std::string error;
    if (!content::readMeshAsset(file, out, info, error)) {
        log::warn("acp: cannot read '%s': %s%s", file.string().c_str(),
                  error.c_str(),
                  allowFallback ? "; falling back to the source model" : "");
        return false;
    }

    const ModelImportOptions requested = sanitizeModelImportOptions(options);
    if (allowFallback && !settingsMatch(info, requested)) {
        warnOnce(file.string(),
                 "acp: '%s' is conditioned for pivot %s but the call site asked "
                 "for %s; importing from source instead. Set it in the .meta to "
                 "get the fast path.",
                 pivotName(info.pivot),
                 pivotName(static_cast<uint8_t>(requested.pivot)));
        out = {};
        return false;
    }

    report = reportFrom(info, out, file);
    return true;
}

} // namespace

bool loadStaticModel(const fs::path& path, const ModelImportOptions& options,
                     ImportedModelData& out, ModelImportReport& report)
{
    // Named directly. There is no source to fall back to, so the file's own
    // settings are the truth and a parse failure is the answer, not a detour.
    if (path.extension() == content::kMeshAssetExtension)
        return readConditioned(path, options, out, report,
                               /*allowFallback=*/false);

    const fs::path conditioned = assets::conditioned(path);
    if (!conditioned.empty() &&
        conditioned.extension() == content::kMeshAssetExtension &&
        readConditioned(conditioned, options, out, report,
                        /*allowFallback=*/true))
        return true;

    return importStaticModel(path, options, out, report);
}

} // namespace eng::detail
