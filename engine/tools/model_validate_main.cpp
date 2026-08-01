#include "render/AssimpLoader.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

void printBounds(const char* label, const eng::MeshBounds& bounds)
{
    std::printf("**%s:** `[%.6g, %.6g, %.6g]` to "
                "`[%.6g, %.6g, %.6g]`\n",
                label, bounds.min.x, bounds.min.y, bounds.min.z,
                bounds.max.x, bounds.max.y, bounds.max.z);
}

const char* pivotName(eng::PivotMode pivot)
{
    switch (pivot) {
    case eng::PivotMode::Source:
        return "source";
    case eng::PivotMode::BoundsCenter:
        return "bounds-center";
    case eng::PivotMode::BottomCenter:
        return "bottom-center";
    case eng::PivotMode::Custom:
        return "custom";
    }
    return "invalid";
}

bool validate(const std::filesystem::path& path,
              const eng::ModelImportOptions& options)
{
    eng::detail::ImportedModelData model;
    eng::ModelImportReport report;
    const bool passed =
        eng::detail::importStaticModel(path, options, model, report);

    std::printf("## Optimization Report: %s\n\n",
                path.filename().string().c_str());
    std::printf("**Source:** `%s`\n", path.string().c_str());
    std::printf("**Status:** %s\n", passed ? "PASS" : "FAIL");
    if (!passed) {
        std::printf("**Error:** %s\n\n", report.error.c_str());
        return false;
    }

    std::printf("**Total Triangles:** %llu (budget: %llu)\n",
                static_cast<unsigned long long>(report.triangles),
                static_cast<unsigned long long>(options.limits.maxTriangles));
    std::printf("**Source Bytes:** %llu (budget: %llu)\n",
                static_cast<unsigned long long>(report.sourceBytes),
                static_cast<unsigned long long>(options.limits.maxSourceBytes));
    std::printf("**Total Vertices:** %llu (budget: %llu)\n",
                static_cast<unsigned long long>(report.vertices),
                static_cast<unsigned long long>(options.limits.maxVertices));
    std::printf("**Draw Calls (submeshes):** %u (budget: %u)\n",
                report.submeshes, options.limits.maxSubmeshes);
    std::printf("**Materials:** %u (budget: %u)\n", report.materials,
                options.limits.maxMaterials);
    std::printf("**Pivot:** %s\n", pivotName(report.appliedPivot));
    std::printf("**Metres Per Source Unit:** %.6g\n",
                report.appliedMetresPerSourceUnit);
    std::printf("**Source Units:** %s\n",
                report.sourceUnitsAssumed
                    ? "assumed; verify --scale for this format"
                    : "format-declared, then import multiplier applied");
    const bool canonical = report.canonicalPivotStandard &&
                           eng::modelImportMeetsSpatialStandard(
                               report.finalBounds);
    std::printf("**Pivot/Grounding Standard:** %s\n",
                report.appliedPivot != eng::PivotMode::BottomCenter
                    ? "NOT APPLIED (explicit pivot override)"
                    : canonical
                          ? "PASS (bottom-center, X/Z centered, grounded Y=0)"
                          : "FAIL");
    printBounds("Source Bounds", report.sourceBounds);
    printBounds("Final Bounds", report.finalBounds);
    std::printf("**Cleanup:** %llu loose vertices, %llu degenerate triangles, "
                "%llu duplicate triangles removed\n\n",
                static_cast<unsigned long long>(report.removedLooseVertices),
                static_cast<unsigned long long>(
                    report.removedDegenerateTriangles),
                static_cast<unsigned long long>(
                    report.removedDuplicateTriangles));

    std::printf("### Per-Object Breakdown\n\n");
    std::printf("| Object | Vertices | Tris | Source Material | Status |\n");
    std::printf("|---|---:|---:|---|---|\n");
    for (const eng::detail::ImportedModelSubmesh& submesh : model.submeshes) {
        std::printf("| %s | %zu | %zu | %s | PASS |\n",
                    submesh.name.c_str(), submesh.vertices.size(),
                    submesh.indices.size() / 3u,
                    submesh.sourceMaterial.empty()
                        ? "(none)"
                        : submesh.sourceMaterial.c_str());
    }
    if (!report.warnings.empty()) {
        std::printf("\n### Warnings\n\n");
        for (const std::string& warning : report.warnings)
            std::printf("- %s\n", warning.c_str());
    }
    std::printf("\n");
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(
            stderr,
            "usage: model_validate [--scale metres-per-unit] "
            "[--pivot bottom|center|source] <model> [model ...]\n");
        return 2;
    }
    eng::ModelImportOptions options;
    std::vector<std::filesystem::path> paths;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--scale" && index + 1 < argc) {
            char* end = nullptr;
            const float scale = std::strtof(argv[++index], &end);
            if (!end || *end != '\0' || !(scale > 0.0f)) {
                std::fprintf(stderr, "model_validate: invalid --scale\n");
                return 2;
            }
            options.metresPerSourceUnit = scale;
        } else if (argument == "--pivot" && index + 1 < argc) {
            const std::string pivot = argv[++index];
            if (pivot == "bottom")
                options.pivot = eng::PivotMode::BottomCenter;
            else if (pivot == "center")
                options.pivot = eng::PivotMode::BoundsCenter;
            else if (pivot == "source")
                options.pivot = eng::PivotMode::Source;
            else {
                std::fprintf(stderr, "model_validate: invalid --pivot\n");
                return 2;
            }
        } else if (argument.starts_with("--")) {
            std::fprintf(stderr, "model_validate: unknown option '%s'\n",
                         argument.c_str());
            return 2;
        } else {
            paths.emplace_back(argument);
        }
    }
    if (paths.empty()) {
        std::fprintf(stderr, "model_validate: no model paths supplied\n");
        return 2;
    }
    bool passed = true;
    for (const std::filesystem::path& path : paths)
        passed = validate(std::filesystem::absolute(path), options) && passed;
    return passed ? 0 : 1;
}
