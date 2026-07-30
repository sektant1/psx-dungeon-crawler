#include "PreviewBridge.h"

#include "SceneCook.h"

#include <eng/Renderer.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/MeshSource.h>
#include <ecs/RendererSceneBackend.h>

#include <filesystem>

namespace ed {

using game::content::AuthorId;

struct PreviewBridge::Impl
{
    Impl(eng::Renderer& renderer, std::string root)
        : assetRoot(std::move(root)), backend(renderer), sync(scene, backend),
          renderer(renderer)
    {
    }

    std::string assetRoot;
    eng::ecs::Scene scene;
    eng::ecs::RendererSceneBackend backend;
    eng::ecs::SceneSync sync;
    eng::Renderer& renderer;

    std::unordered_map<AuthorId, entt::entity> authorToEntity;
    std::unordered_map<AuthorId, eng::NodeHandle> authorToNode;
    // Meshes are cached across rebuilds: a full rebuild happens on every edit,
    // and re-parsing an OBJ per keystroke would make the editor unusable.
    std::unordered_map<std::string, eng::MeshHandle> meshCache;
    uint64_t builtRevision = ~uint64_t(0);
    bool visible = true;
};

PreviewBridge::PreviewBridge(eng::Renderer& renderer,
                             const std::string& assetRoot)
    : mImpl(std::make_unique<Impl>(renderer, assetRoot))
{
}

PreviewBridge::~PreviewBridge() = default;

void PreviewBridge::invalidate() { mImpl->builtRevision = ~uint64_t(0); }

void PreviewBridge::sync(const game::content::SceneDocument& document,
                         const game::content::KitCatalog& catalog)
{
    if (mImpl->builtRevision == document.revision)
        return;
    mImpl->builtRevision = document.revision;
    mError.clear();

    // Rebuild from scratch. Wasteful per keystroke, and deliberately so for
    // now: a correct full rebuild is the baseline the incremental path has to
    // match, and the scenes this edits are hundreds of entities, not millions.
    mImpl->sync.clear();
    mImpl->authorToNode.clear();
    entt::registry& registry = mImpl->scene.registry();
    if (!game::content::buildRegistry(document, catalog, registry, mError,
                                      &mImpl->authorToEntity)) {
        // A document with an unresolved prefab still has to be editable, so
        // this is a message in the UI, not a fatal error.
        return;
    }

    // MeshSource (a relative path) -> a loaded mesh handle. Same resolution the
    // runtime does, minus the absolute-path fallback: scenes are portable.
    auto view = registry.view<eng::ecs::MeshSource, eng::ecs::MeshRenderer>();
    for (const entt::entity entity : view) {
        const std::string& path = view.get<eng::ecs::MeshSource>(entity).path;
        auto cached = mImpl->meshCache.find(path);
        if (cached == mImpl->meshCache.end()) {
            const std::filesystem::path full =
                std::filesystem::path(mImpl->assetRoot) / path;
            std::error_code code;
            const eng::MeshHandle mesh =
                std::filesystem::exists(full, code)
                    ? mImpl->renderer.loadObj(full.string())
                    : mImpl->renderer.prototypeMesh(path);
            cached = mImpl->meshCache.emplace(path, mesh).first;
        }
        view.get<eng::ecs::MeshRenderer>(entity).mesh = cached->second;
    }

    mImpl->sync.sync();

    for (const auto& [id, entity] : mImpl->authorToEntity) {
        if (const auto* node = registry.try_get<eng::ecs::NodeRef>(entity)) {
            mImpl->authorToNode[id] = node->handle;
            // A rebuild while the level is hidden must not bring it back.
            if (!mImpl->visible)
                mImpl->renderer.setNodeVisible(node->handle, false);
        }
    }
}

void PreviewBridge::setVisible(eng::Renderer& renderer, bool visible)
{
    for (const auto& [id, node] : mImpl->authorToNode) {
        (void)id;
        renderer.setNodeVisible(node, visible);
    }
    mImpl->visible = visible;
}

const eng::NodeHandle* PreviewBridge::nodeFor(const AuthorId& id) const
{
    const auto found = mImpl->authorToNode.find(id);
    return found == mImpl->authorToNode.end() ? nullptr : &found->second;
}

} // namespace ed
