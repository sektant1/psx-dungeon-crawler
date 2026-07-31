#include "PreviewBridge.h"

#include "SceneCook.h"

#include <eng/Renderer.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/MeshSource.h>
#include <ecs/RendererSceneBackend.h>

#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <limits>

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
    // The height each node stands at, so the ceiling cut can be re-applied
    // after a rebuild without walking the document again.
    std::unordered_map<AuthorId, float> authorToHeight;
    // Meshes are cached across rebuilds: a full rebuild happens on every edit,
    // and re-parsing an OBJ per keystroke would make the editor unusable.
    std::unordered_map<std::string, eng::MeshHandle> meshCache;
    std::unordered_map<std::string, eng::NodeHandle> ghostNodes;
    std::string visibleGhost;
    uint64_t builtRevision = ~uint64_t(0);
    bool visible = true;
    float ceilingCut = std::numeric_limits<float>::infinity();

    bool cutAway(const AuthorId& id) const
    {
        const auto found = authorToHeight.find(id);
        return found != authorToHeight.end() && found->second > ceilingCut;
    }

    eng::MeshHandle meshFor(const std::string& path)
    {
        auto cached = meshCache.find(path);
        if (cached != meshCache.end())
            return cached->second;
        const std::filesystem::path full =
            std::filesystem::path(assetRoot) / path;
        std::error_code code;
        const eng::MeshHandle mesh =
            std::filesystem::exists(full, code) ? renderer.loadObj(full.string())
                                                : renderer.prototypeMesh(path);
        return meshCache.emplace(path, mesh).first->second;
    }

    void hideGhost()
    {
        if (visibleGhost.empty())
            return;
        const auto found = ghostNodes.find(visibleGhost);
        if (found != ghostNodes.end())
            renderer.setNodeVisible(found->second, false);
        visibleGhost.clear();
    }

    ~Impl()
    {
        for (const auto& [path, node] : ghostNodes) {
            (void)path;
            renderer.destroyNode(node);
        }
    }
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
    mImpl->authorToHeight.clear();
    for (const game::content::Entity& entity : document.entities)
        mImpl->authorToHeight[entity.id] = entity.transform.position.y;
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
        view.get<eng::ecs::MeshRenderer>(entity).mesh = mImpl->meshFor(path);
    }

    mImpl->sync.sync();

    for (const auto& [id, entity] : mImpl->authorToEntity) {
        if (const auto* node = registry.try_get<eng::ecs::NodeRef>(entity)) {
            mImpl->authorToNode[id] = node->handle;
            // A rebuild must not bring back what is hidden -- neither the whole
            // level in a mode that took the viewport, nor a storey the cut is
            // holding open.
            if (!mImpl->visible || mImpl->cutAway(id))
                mImpl->renderer.setNodeVisible(node->handle, false);
        }
    }
}

void PreviewBridge::setVisible(eng::Renderer& renderer, bool visible)
{
    for (const auto& [id, node] : mImpl->authorToNode)
        renderer.setNodeVisible(node, visible && !mImpl->cutAway(id));
    if (!visible)
        mImpl->hideGhost();
    mImpl->visible = visible;
}

void PreviewBridge::setCeilingCut(eng::Renderer& renderer, float height)
{
    if (mImpl->ceilingCut == height)
        return;
    mImpl->ceilingCut = height;
    if (!mImpl->visible)
        return;
    for (const auto& [id, node] : mImpl->authorToNode)
        renderer.setNodeVisible(node, !mImpl->cutAway(id));
}

void PreviewBridge::showPlacementGhost(
    const game::content::KitPiece& piece,
    const game::content::XformAuthor& transform, float importScale)
{
    if (!mImpl->visible || piece.meshPath.empty()) {
        mImpl->hideGhost();
        return;
    }

    if (mImpl->visibleGhost != piece.meshPath)
        mImpl->hideGhost();

    auto found = mImpl->ghostNodes.find(piece.meshPath);
    if (found == mImpl->ghostNodes.end()) {
        const eng::NodeHandle node = mImpl->renderer.createNode(
            eng::kRootNode, glm::vec3(0.0f), "editor_placement_ghost");
        mImpl->renderer.attachMesh(node, mImpl->meshFor(piece.meshPath),
                                   "__Editor/PlacementGhost", false);
        found = mImpl->ghostNodes.emplace(piece.meshPath, node).first;
    }

    const eng::NodeHandle node = found->second;
    mImpl->renderer.setPosition(node, transform.position);
    mImpl->renderer.setOrientation(
        node, game::content::authorOrientation(transform.rotationDegrees));
    mImpl->renderer.setScale(node, transform.scale * importScale);
    mImpl->renderer.setNodeVisible(node, true);
    mImpl->visibleGhost = piece.meshPath;
}

void PreviewBridge::hidePlacementGhost()
{
    mImpl->hideGhost();
}

bool PreviewBridge::entityVisible(const AuthorId& id) const
{
    return mImpl->visible && !mImpl->cutAway(id);
}

const eng::NodeHandle* PreviewBridge::nodeFor(const AuthorId& id) const
{
    const auto found = mImpl->authorToNode.find(id);
    return found == mImpl->authorToNode.end() ? nullptr : &found->second;
}

} // namespace ed
