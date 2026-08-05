#include "Exporters.h"

#include <algorithm>

namespace eng::acp {

std::string Exporter::outputFor(const Record& record) const
{
    const std::string intermediate =
        content::intermediatePathFor(record.logical);
    // Empty means the asset is already an intermediate (a .map beside its .scn)
    // or the row has no conversion. Either way it keeps its own name.
    return intermediate.empty() ? record.logical : intermediate;
}

ExporterRegistry::ExporterRegistry() : mPublisher(makeCopyExporter()) {}

void ExporterRegistry::add(std::unique_ptr<Exporter> exporter)
{
    if (!exporter)
        return;
    const AssetType type = exporter->type();
    // Last registration wins, which is what lets the CLI supply the World row
    // (whose cooker lives in game_content) on top of the built-ins, and what
    // would let a project override the mesh exporter without editing this file.
    const auto existing =
        std::find_if(mExporters.begin(), mExporters.end(),
                     [type](const std::unique_ptr<Exporter>& candidate) {
                         return candidate->type() == type;
                     });
    if (existing != mExporters.end())
        *existing = std::move(exporter);
    else
        mExporters.push_back(std::move(exporter));
}

const Exporter* ExporterRegistry::find(AssetType type) const
{
    for (const std::unique_ptr<Exporter>& exporter : mExporters)
        if (exporter->type() == type)
            return exporter.get();
    return nullptr;
}

std::vector<const Exporter*> ExporterRegistry::all() const
{
    std::vector<const Exporter*> out;
    out.reserve(mExporters.size());
    for (const std::unique_ptr<Exporter>& exporter : mExporters)
        out.push_back(exporter.get());
    return out;
}

void registerBuiltinExporters(ExporterRegistry& registry)
{
    registry.add(makeMeshExporter());
    registry.add(makeTextureExporter());
    registry.add(makeParticleExporter());
    registry.add(makeSoundBankExporter());
    registry.add(makeObjectTemplateExporter());
    registry.add(makeAnimationTreeExporter());
    for (std::unique_ptr<Exporter>& exporter : makePassthroughExporters())
        registry.add(std::move(exporter));
}

} // namespace eng::acp
