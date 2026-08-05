#include <editor/content/SceneWorldExporter.h>

#include <editor/content/KitCatalog.h>
#include <editor/content/SceneCook.h>
#include <editor/content/SceneDocument.h>
#include <editor/content/SceneSource.h>
#include <editor/content/SceneValidate.h>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>

namespace fs = std::filesystem;

namespace ed {
namespace {

using eng::acp::AssetType;
using eng::acp::ExportContext;
using eng::acp::ExportResult;
using eng::acp::Exporter;

// The kit is read once per path and shared: it is ~1600 lines of TOML and every
// scene in the tree wants the same one, so parsing it per scene would dominate
// the cook. Guarded rather than thread_local because the pipeline runs scenes in
// parallel and a per-thread copy would be the same waste with more copies.
//
// Cached in a NODE-BASED map, and entries are never evicted, so a pointer handed
// out stays valid after another thread asks for a different kit. The obvious
// version -- one member catalogue, reloaded when the path changes -- returns a
// pointer into storage the next caller may overwrite while the first is still
// reading it. Every scene here names the same kit, so it never fired; it was one
// kit-per-scene away from a race that only shows up under load.
class SharedKit {
public:
    const game::content::KitCatalog* get(const fs::path& path, std::string& error)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto existing = mLoaded.find(path);
        if (existing != mLoaded.end()) {
            error = existing->second.error;
            return existing->second.catalog ? existing->second.catalog.get()
                                            : nullptr;
        }
        Entry entry;
        auto catalog = std::make_unique<game::content::KitCatalog>();
        if (game::content::KitCatalog::load(path.string(), *catalog, entry.error))
            entry.catalog = std::move(catalog);
        error = entry.error;
        const game::content::KitCatalog* result = entry.catalog.get();
        mLoaded.emplace(path, std::move(entry));
        return result;
    }

private:
    struct Entry {
        std::unique_ptr<game::content::KitCatalog> catalog;
        std::string error;
    };
    std::mutex mMutex;
    std::map<fs::path, Entry> mLoaded;
};

class SceneWorldExporter final : public Exporter {
public:
    std::string_view name() const override { return "World Editor"; }
    AssetType type() const override { return AssetType::World; }
    // 1: .map via game::content::cookToMap.
    uint32_t version() const override { return 1; }

    ExportResult run(const ExportContext& context) const override
    {
        ExportResult result;

        const fs::path kitPath = context.contentRoot / "config" / "kit.toml";
        std::string kitError;
        const game::content::KitCatalog* catalog =
            mKit.get(kitPath, kitError);
        if (!catalog) {
            result.error = "kit: " + kitError;
            return result;
        }
        // A scene is only as current as the kit it was cooked against: swap a
        // prefab's mesh and every map that places it has to be rebuilt.
        result.dependencies.push_back("config/kit.toml");

        game::content::SceneDocument document;
        if (!game::content::loadSceneSource(context.sourcePath.string(), document,
                                            result.error))
            return result;

        // The same validation the editor and scene_cook run, so "it cooked on
        // my machine" cannot start happening through a third entry point.
        const std::vector<game::content::Issue> issues = game::content::validate(
            document, *catalog, context.contentRoot.string());
        for (const game::content::Issue& issue : issues) {
            const std::string text =
                issue.code + ": " + issue.message +
                (issue.entity.empty() ? "" : " -- entity " + issue.entity);
            if (issue.severity == game::content::Severity::Error)
                result.error = text;
            else
                result.warnings.push_back(text);
        }
        if (game::content::blocksCook(issues)) {
            if (result.error.empty())
                result.error = "scene has blocking issues";
            return result;
        }
        result.error.clear();

        // Every mesh the scene's prefabs reach is a dependency. This is what
        // makes "I changed a wall mesh" rebuild the levels that use it, which
        // was previously a thing a person had to know.
        for (const game::content::KitPiece& piece : catalog->all())
            if (!piece.meshPath.empty())
                result.dependencies.push_back(piece.meshPath);

        if (!game::content::cookToMap(document, *catalog,
                                      context.outputPath.string(), result.error))
            return result;
        result.ok = true;
        return result;
    }

private:
    mutable SharedKit mKit;
};

} // namespace

std::unique_ptr<Exporter> makeSceneWorldExporter()
{
    return std::make_unique<SceneWorldExporter>();
}

} // namespace ed
