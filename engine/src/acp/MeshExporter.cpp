// The diagram's "Maya, 3ds Max, Blender -> Mesh Exporter -> Mesh" row.
//
// Assimp reads the DCC file; eng::detail::importStaticModel applies this
// engine's canonical orientation, unit scale and pivot; the result is written
// as .rmesh. That import function is the same one Renderer::loadMesh called at
// runtime before this existed, which is the point: the conversion did not move
// or change, it stopped happening 60 times per launch.

#include "Exporters.h"

#include <eng/content/MeshAsset.h>
#include <eng/render/ModelImport.h>

#include "render/AssimpLoader.h"


namespace eng::acp {
namespace {

PivotMode pivotFromName(std::string_view name)
{
    if (name == "source")
        return PivotMode::Source;
    if (name == "bounds_center")
        return PivotMode::BoundsCenter;
    if (name == "custom")
        return PivotMode::Custom;
    return PivotMode::BottomCenter;
}

TexcoordVMode texcoordFromName(std::string_view name)
{
    if (name == "preserve")
        return TexcoordVMode::Preserve;
    if (name == "flip")
        return TexcoordVMode::Flip;
    return TexcoordVMode::FormatDefault;
}

class MeshExporter final : public Exporter {
public:
    std::string_view name() const override { return "Mesh Exporter"; }
    AssetType type() const override { return AssetType::Mesh; }
    // 1: initial .rmesh.
    uint32_t version() const override { return 1; }

    ExportResult run(const ExportContext& context) const override
    {
        ExportResult result;

        const ModelImportSettings settings =
            meshImportSettings(context.record->import);
        ModelImportOptions options;
        options.pivot = static_cast<PivotMode>(settings.pivot);
        options.texcoordV = static_cast<TexcoordVMode>(settings.texcoordV);
        options.metresPerSourceUnit = settings.metresPerSourceUnit;
        options = sanitizeModelImportOptions(options);

        content::MeshData mesh;
        ModelImportReport report;
        if (!detail::importStaticModel(context.sourcePath, options, mesh,
                                       report)) {
            result.error = report.error.empty() ? "import failed" : report.error;
            return result;
        }
        result.warnings = report.warnings;

        // Collision geometry is a per-asset decision the resource database owns:
        // a 200k-triangle hero prop that is never a static collider should not
        // carry a second copy of itself into the pack.
        if (!settings.generateCollision) {
            mesh.collisionVertices.clear();
            mesh.collisionIndices.clear();
        }

        content::MeshAssetInfo info;
        info.sourcePath = context.record->logical;
        info.format = report.format;
        info.sourceBytes = report.sourceBytes;
        info.sourceMeshes = report.sourceMeshes;
        info.materials = report.materials;
        info.boundsMin = report.finalBounds.min;
        info.boundsMax = report.finalBounds.max;
        info.metresPerSourceUnit = options.metresPerSourceUnit;
        info.sourceOrientation = {options.sourceOrientation.w,
                                  options.sourceOrientation.x,
                                  options.sourceOrientation.y,
                                  options.sourceOrientation.z};
        info.customPivot = options.customPivot;
        info.pivot = static_cast<uint8_t>(options.pivot);
        info.texcoordV = static_cast<uint8_t>(options.texcoordV);
        info.canonicalPivotStandard = report.canonicalPivotStandard;

        // Every texture the source materials name becomes a dependency, so
        // re-exporting a model after its textures change is not something an
        // artist has to remember. Names that are absolute or embedded ("*0")
        // are recorded but cannot be resolved against the content root, and
        // hashing a path that is not in the tree would key on nothing.
        for (const content::MeshSubmesh& submesh : mesh.submeshes) {
            if (submesh.sourceTexture.empty() ||
                submesh.sourceTexture[0] == '*' ||
                std::filesystem::path(submesh.sourceTexture).is_absolute())
                continue;
            const std::filesystem::path relative =
                std::filesystem::path(context.record->logical).parent_path() /
                submesh.sourceTexture;
            result.dependencies.push_back(relative.lexically_normal().generic_string());
        }

        if (!writeMeshAsset(context.outputPath, mesh, info, result.error))
            return result;
        result.ok = true;
        return result;
    }
};

} // namespace

ModelImportSettings meshImportSettings(const content::Settings& settings)
{
    ModelImportSettings out;
    out.pivot = static_cast<int>(
        pivotFromName(content::settingString(settings, "pivot", "bottom_center")));
    out.texcoordV = static_cast<int>(texcoordFromName(
        content::settingString(settings, "texcoord_v", "format_default")));
    out.metresPerSourceUnit = static_cast<float>(
        content::settingNumber(settings, "metres_per_source_unit", 1.0));
    out.generateCollision =
        content::settingBool(settings, "generate_collision", true);
    return out;
}

std::unique_ptr<Exporter> makeMeshExporter()
{
    return std::make_unique<MeshExporter>();
}

} // namespace eng::acp
