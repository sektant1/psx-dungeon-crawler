// Four rows of the diagram that share one shape:
//
//   Houdini / Custom Particle Authoring Tool -> Particle Exporter -> Particle System
//   Audio Management Tool                    ->                   -> Sound Bank
//   Object Model Editor                      ->                   -> Game Obj. Templates
//   Animation Tree Editor                    ->                   -> Animation Tree
//
// All four are authored as TOML and all four are parsed at start-up. The
// conversion is TOML document -> ordered typed tree -> binary, which takes the
// parser out of the runtime without inventing a second schema per row: a new
// authoring key needs no pipeline work at all, because the exporter conditions
// the document rather than a struct.
//
// The rows differ only in their extension, their exporter name, and how they
// find their dependencies -- which files this asset would want rebuilding for.

#include "Exporters.h"

#include <eng/content/DataAsset.h>

#include <algorithm>
#include <filesystem>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

namespace fs = std::filesystem;

namespace eng::acp {
namespace {

content::DataValue convert(const toml::node& node)
{
    if (const auto* value = node.as_boolean())
        return content::DataValue::makeBool(value->get());
    if (const auto* value = node.as_integer())
        return content::DataValue::makeInteger(value->get());
    if (const auto* value = node.as_floating_point())
        return content::DataValue::makeNumber(value->get());
    if (const auto* value = node.as_string())
        return content::DataValue::makeString(value->get());
    if (const auto* array = node.as_array()) {
        content::DataArray out;
        out.reserve(array->size());
        for (const toml::node& entry : *array)
            out.push_back(convert(entry));
        return content::DataValue::makeArray(std::move(out));
    }
    if (const auto* table = node.as_table()) {
        content::DataTable out;
        out.reserve(table->size());
        for (const auto& [key, entry] : *table)
            out.emplace_back(std::string(key.str()), convert(entry));
        return content::DataValue::makeTable(std::move(out));
    }
    // Dates and times. TOML has them, no authored file here uses one, and
    // silently dropping a value would be worse than the empty it becomes --
    // which the caller reports as a warning.
    return {};
}

void collectStrings(const content::DataValue& value,
                    std::vector<std::string>& out)
{
    switch (value.kind()) {
    case content::DataValue::Kind::String:
        out.push_back(value.asString());
        break;
    case content::DataValue::Kind::Array:
        for (const content::DataValue& entry : value.asArray())
            collectStrings(entry, out);
        break;
    case content::DataValue::Kind::Table:
        for (const auto& [key, entry] : value.asTable())
            collectStrings(entry, out);
        break;
    default:
        break;
    }
}

// Any authored string that names a file in the content tree is a dependency.
// Checked against the root and against the document's own directory, which
// between them cover every path convention in this tree -- audio.toml's
// "audio/foley/step.wav" and a sibling reference alike. A false positive costs
// one unnecessary rebuild; a false negative ships a stale bank.
void addPathDependencies(const ExportContext& context,
                         const content::DataValue& root,
                         const std::vector<fs::path>& extraSearchDirs,
                         std::vector<std::string>& dependencies)
{
    std::vector<std::string> strings;
    collectStrings(root, strings);

    const fs::path sourceDir =
        fs::path(context.record->logical).parent_path();

    std::vector<fs::path> bases;
    bases.emplace_back(); // content root itself
    bases.push_back(sourceDir);
    bases.insert(bases.end(), extraSearchDirs.begin(), extraSearchDirs.end());

    for (const std::string& text : strings) {
        if (text.empty() || text.size() > 260 ||
            text.find('.') == std::string::npos)
            continue;
        for (const fs::path& base : bases) {
            const fs::path logical = (base / text).lexically_normal();
            std::error_code ec;
            if (!fs::is_regular_file(context.contentRoot / logical, ec))
                continue;
            const std::string generic = logical.generic_string();
            if (std::find(dependencies.begin(), dependencies.end(), generic) ==
                dependencies.end())
                dependencies.push_back(generic);
            break;
        }
    }
}

// How a row finds the directories its relative references are rooted at.
// kit.toml is the one that needs it: its pieces name "Floor_Tiles.obj" and the
// document's own `[kit] mesh_dir` says where that lives.
using SearchDirsFn =
    std::vector<fs::path> (*)(const content::DataValue& root);

std::vector<fs::path> noSearchDirs(const content::DataValue&) { return {}; }

std::vector<fs::path> kitSearchDirs(const content::DataValue& root)
{
    const std::string meshDir = root.at("kit.mesh_dir").asString();
    if (meshDir.empty())
        return {};
    return {fs::path(meshDir)};
}

class DataExporter final : public Exporter {
public:
    DataExporter(std::string_view boxName, AssetType type, SearchDirsFn searchDirs)
        : mName(boxName), mType(type), mSearchDirs(searchDirs)
    {
    }

    std::string_view name() const override { return mName; }
    AssetType type() const override { return mType; }
    // 1: initial RAVENDAT encoding.
    uint32_t version() const override { return 1; }

    ExportResult run(const ExportContext& context) const override
    {
        ExportResult result;

        const toml::parse_result parsed =
            toml::parse_file(context.sourcePath.string());
        if (!parsed) {
            result.error = std::string(parsed.error().description());
            return result;
        }

        content::DataAsset asset;
        asset.sourcePath = context.record->logical;
        asset.root = convert(parsed.table());
        if (asset.root.kind() != content::DataValue::Kind::Table) {
            result.error = "document root is not a table";
            return result;
        }

        addPathDependencies(context, asset.root, mSearchDirs(asset.root),
                            result.dependencies);

        if (!content::writeDataAsset(context.outputPath, asset, result.error))
            return result;
        result.ok = true;
        return result;
    }

private:
    std::string_view mName;
    AssetType mType;
    SearchDirsFn mSearchDirs;
};

} // namespace

std::unique_ptr<Exporter> makeParticleExporter()
{
    return std::make_unique<DataExporter>("Particle Exporter",
                                          AssetType::ParticleSystem,
                                          noSearchDirs);
}

std::unique_ptr<Exporter> makeSoundBankExporter()
{
    return std::make_unique<DataExporter>("Audio Management Tool",
                                          AssetType::SoundBank, noSearchDirs);
}

std::unique_ptr<Exporter> makeObjectTemplateExporter()
{
    return std::make_unique<DataExporter>("Object Model Editor",
                                          AssetType::ObjectTemplate,
                                          kitSearchDirs);
}

std::unique_ptr<Exporter> makeAnimationTreeExporter()
{
    return std::make_unique<DataExporter>("Animation Tree Exporter",
                                          AssetType::AnimationTree, noSearchDirs);
}

} // namespace eng::acp
