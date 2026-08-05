// The rows the diagram routes straight into the pipeline with no processor box
// between the DCC arrow and the grey stage.
//
//   Material          straight off the Maya/Z-Brush/Substance arrow
//   WAV sound         a Sound Bank member, referenced by the bank
//   Skel. Hierarchy   .skeleton.ozz, written by gltf2ozz
//   Animation Clips   clip_*.ozz, likewise
//
// plus the engine data the runtime reads by path and no exporter improves:
// shaders, fonts, Lua scripts, UI text and the config TOMLs the game reads by
// name rather than as a bank or a template.
//
// "Pass-through" is not "ignored". Publishing them means the pack is complete
// -- the game can be pointed at the cooked directory and find everything, which
// is what makes the ACP the boundary the diagram draws rather than a partial
// overlay. It also means every one of them is in the resource database, has a
// guid, is hashed, and can be named as a dependency by an asset that is
// converted.

#include "Exporters.h"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace eng::acp {
namespace {

class PassthroughExporter final : public Exporter {
public:
    PassthroughExporter(std::string_view boxName, AssetType type)
        : mName(boxName), mType(type)
    {
    }

    std::string_view name() const override { return mName; }
    AssetType type() const override { return mType; }
    uint32_t version() const override { return 1; }

    ExportResult run(const ExportContext& context) const override
    {
        ExportResult result;
        std::error_code ec;
        if (!context.outputPath.parent_path().empty()) {
            fs::create_directories(context.outputPath.parent_path(), ec);
            if (ec) {
                result.error = "cannot create output directory: " + ec.message();
                return result;
            }
        }
        // overwrite_existing rather than a remove-then-copy: the destination is
        // a file the game may currently have open, and replacing it in one call
        // is the closest this gets to atomic without a temp-and-rename dance
        // that copy_file does not offer.
        fs::copy_file(context.sourcePath, context.outputPath,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            result.error = "cannot copy: " + ec.message();
            return result;
        }
        result.ok = true;
        return result;
    }

private:
    std::string_view mName;
    AssetType mType;
};

} // namespace

std::unique_ptr<Exporter> makeCopyExporter()
{
    return std::make_unique<PassthroughExporter>("Publish", AssetType::Unknown);
}

std::vector<std::unique_ptr<Exporter>> makePassthroughExporters()
{
    std::vector<std::unique_ptr<Exporter>> out;
    const auto add = [&out](std::string_view boxName, AssetType type) {
        out.push_back(std::make_unique<PassthroughExporter>(boxName, type));
    };
    // The names are the diagram's where it has one, and the producing tool's
    // where the diagram leaves the arrow unlabelled.
    add("Material", AssetType::Material);
    add("Sound Forge / Logic Pro / REAPER", AssetType::Sound);
    add("gltf2ozz (skeleton)", AssetType::Skeleton);
    add("gltf2ozz (animation)", AssetType::Animation);
    add("Shader", AssetType::Shader);
    add("Font", AssetType::Font);
    add("Script", AssetType::Script);
    add("Config", AssetType::Config);
    add("UI", AssetType::Ui);
    return out;
}

} // namespace eng::acp
