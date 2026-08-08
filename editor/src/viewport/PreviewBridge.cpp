#include <editor/viewport/PreviewBridge.h>

#include <editor/content/SceneCook.h>

#include <scene/ComponentRegistry.h> // mapio::coreRegistry(), for clip tracks

#include <FirstPersonHands.h>
#include <HandsDefinition.h>
#include <PlayerWeapons.h>
#include <ViewmodelRig.h>

#include <eng/Renderer.h>
#include <eng/assets/AssetRoot.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/Systems.h>
#include <eng/ecs/components/MeshSource.h>
#include <eng/particles/ParticleEffectDesc.h>
#include <eng/ecs/RendererSceneBackend.h>

#include <filesystem>
#include <chrono>
#include <cstdio>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <unordered_set>

namespace ed {

using game::content::AuthorId;

struct PreviewBridge::Impl
{
    explicit Impl(eng::Renderer& renderer) : backend(renderer), renderer(renderer)
    {
        // The editor preview is a world of its own, not half of the game's:
        // a different simulation, with no physics and no gameplay on it.
        //
        // It does NOT drive the camera. The cameras in the document are
        // content the author is placing, and the viewport belongs to the
        // EditorCamera -- a preview that attached the renderer's camera to an
        // authored one would throw the author out of their own view on every
        // rebuild, which is once per keystroke.
        world.attachRenderer(backend, /*drivesCamera=*/false);
        // The same table the cooker and the inspector use. A clip addresses a
        // component by name, so without this the Timeline could scrub a clip
        // in the editor and see nothing move (see docs/clips.md). It is a
        // function-local static and so outlives this world.
        world.setComponentTypes(&mapio::coreRegistry());
    }

    eng::ecs::RendererSceneBackend backend;
    eng::ecs::World world;
    eng::Renderer& renderer;

    std::unordered_map<AuthorId, entt::entity> authorToEntity;
    std::unordered_map<AuthorId, eng::NodeHandle> authorToNode;
    // The height each node stands at, so the ceiling cut can be re-applied
    // after a rebuild without walking the document again.
    std::unordered_map<AuthorId, float> authorToHeight;
    // Meshes are cached across rebuilds: a full rebuild happens on every edit,
    // and re-parsing an OBJ per keystroke would make the editor unusable.
    std::unordered_map<std::string, eng::MeshHandle> meshCache;
    // Generated meshes, cached the same way and for the same reason. The same
    // cache the runtime uses, so an authored primitive is the same geometry in
    // the viewport and in the game rather than two implementations of "box".
    eng::ecs::PrimitiveMeshCache primitives;
    // The brush under the cursor, as renderer nodes.
    //
    // A rig rather than a node, because a placeable is not always one mesh: a
    // compound piece is a root plus its attachments, and an imported model is a
    // mesh-less root plus every submesh it arrived as. Drawing only the root
    // showed a boss with no sword and an imported model as nothing at all --
    // and "what will land here" is the entire job of a ghost.
    //
    // The parts hang off the root as child nodes, at the same local offsets and
    // local scales the cooker gives their ECS children, so the ghost and the
    // placed object are the same arrangement by construction.
    struct GhostRig {
        eng::NodeHandle root;
        std::vector<eng::NodeHandle> parts;
        // Union of every part's bounds, in the root's own frame, for the wire
        // box and for callers asking how big the brush is.
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
        bool hasBounds = false;
    };
    std::unordered_map<std::string, GhostRig> ghostNodes;
    std::string visibleGhost;
    eng::NodeHandle particleGhostNode;
    eng::ParticlesHandle particleGhost;

    // --- the first-person viewmodel preview -------------------------------
    // The real rig, the real socket set, the real weapon presentation -- the
    // same classes the game runs, not an editor approximation of them. That is
    // the whole value: an approximation would tell you about the editor.
    //
    // It hangs off a node this bridge owns rather than off the previewed
    // entity's node, because the ECS preview is torn down and rebuilt on every
    // keystroke and reloading a skeleton and a skinned mesh at that rate would
    // make the editor unusable. The node is placed to match the authored
    // camera instead, which is the same transform by a cheaper route.
    eng::NodeHandle viewmodelRoot;
    game::PlayerWeaponLibrary weapons;
    game::HandsDefinition handsDefinition = game::defaultHandsDefinition();
    game::FirstPersonHands hands;
    bool viewmodelLoaded = false;
    bool handsBuilt = false;
    // Set once the rig has been tried and refused to come up. Without it a
    // checkout missing the cooked arms re-parses the skeleton on every
    // keystroke, which is the one place this preview could make the editor
    // slower than it was before it existed.
    bool handsFailed = false;
    std::string builtWeapon;   // the id currently in the hands
    bool viewmodelShown = false;
    // The entity carrying the Viewmodel Preview component, so isolation can
    // decide whether the hands belong on screen with the rest of the subtree.
    AuthorId viewmodelHost;

    // Loads weapons.toml and viewmodel_hands.toml once, lazily: an editor
    // session that never opens a scene with a preview should not pay for them.
    void loadViewmodelContent()
    {
        if (viewmodelLoaded)
            return;
        viewmodelLoaded = true;
        if (const std::filesystem::path path =
                eng::assets::resolve("config/weapons.toml");
            !path.empty())
            weapons.load(path.string());
        if (const std::filesystem::path path =
                eng::assets::resolve("config/viewmodel_hands.toml");
            !path.empty())
        {
            game::HandsLibrary library;
            if (game::loadHandsLibrary(path.string(), library))
                handsDefinition = library.active();
        }
    }

    void hideViewmodel()
    {
        if (viewmodelShown && viewmodelRoot.valid())
            renderer.setNodeVisible(viewmodelRoot, false);
        viewmodelShown = false;
    }

    std::string particleGhostEffect;
    std::chrono::steady_clock::time_point particleGhostRestart;
    uint64_t builtRevision = ~uint64_t(0);
    bool visible = true;
    float ceilingCut = std::numeric_limits<float>::infinity();

    // Entities the outliner's eye has switched off. A set rather than a flag
    // on the node, because the node is rebuilt on every edit and the choice
    // must survive that.
    std::unordered_set<AuthorId> hidden;

    // The isolated subtree: the root and its descendants, or empty when the
    // viewport is showing the whole level. Inverted sense from `hidden` -- this
    // is what stays, not what goes -- because an isolated object is a handful
    // of entities and the level around it is hundreds.
    std::unordered_set<AuthorId> isolated;
    bool isolating = false;

    // Everything that keeps an entity off screen, in one place: isolation, the
    // ceiling cut, and the author's own choice. Picking asks the same function,
    // so an entity hidden by any of them cannot be clicked through either.
    bool cutAway(const AuthorId& id) const
    {
        // First, because it is the strongest: an entity outside the isolated
        // subtree is gone regardless of what the other two think.
        if (isolating && isolated.count(id) == 0)
            return true;
        if (hidden.count(id) != 0)
            return true;
        const auto found = authorToHeight.find(id);
        return found != authorToHeight.end() && found->second > ceilingCut;
    }

    eng::MeshHandle meshFor(const std::string& path)
    {
        auto cached = meshCache.find(path);
        if (cached != meshCache.end())
            return cached->second;
        // A MeshSource is a pack-relative path ("meshes/kit/Door_01.obj"),
        // which is exactly what the resolver takes. An unresolved one draws as
        // the prototype box, as it always has: an editor must stay usable with
        // a broken reference in the document.
        const std::filesystem::path full = eng::assets::resolve(path);
        eng::ModelImportOptions legacyImport;
        legacyImport.pivot = eng::PivotMode::Source;
        const eng::MeshHandle mesh = full.empty()
                                         ? renderer.prototypeMesh(path)
                                         : renderer.loadMesh(full.string(),
                                                             legacyImport);
        return meshCache.emplace(path, mesh).first->second;
    }

    // Puts a rig where the brush is. Shared by every ghost kind, because where
    // a ghost goes has nothing to do with what it is made of.
    void showRig(const GhostRig& rig,
                 const game::content::XformAuthor& transform, float importScale)
    {
        renderer.setPosition(rig.root, transform.position);
        renderer.setOrientation(
            rig.root,
            game::content::authorOrientation(transform.rotationDegrees));
        renderer.setScale(rig.root, transform.scale * importScale);
        renderer.setNodeVisible(rig.root, true);
    }

    void hideGhost()
    {
        if (!visibleGhost.empty()) {
            const auto found = ghostNodes.find(visibleGhost);
            if (found != ghostNodes.end())
                renderer.setNodeVisible(found->second.root, false);
            visibleGhost.clear();
        }
        if (particleGhost.valid()) {
            renderer.despawnParticles(particleGhost);
            particleGhost = {};
        }
        particleGhostEffect.clear();
    }

    ~Impl()
    {
        for (const auto& [path, rig] : ghostNodes) {
            (void)path;
            // Parts are children of the root; destroying the root takes them.
            renderer.destroyNode(rig.root);
        }
        if (particleGhost.valid())
            renderer.despawnParticles(particleGhost);
        if (particleGhostNode.valid())
            renderer.destroyNode(particleGhostNode);
        // The generated meshes are this preview's, not the renderer's: nothing
        // else holds a handle to them, so closing the editor's document has to
        // give the buffers back.
        primitives.clear(renderer);
    }
};

PreviewBridge::PreviewBridge(eng::Renderer& renderer)
    : mImpl(std::make_unique<Impl>(renderer))
{
}

PreviewBridge::~PreviewBridge() = default;

void PreviewBridge::invalidate() { mImpl->builtRevision = ~uint64_t(0); }

void PreviewBridge::sync(const game::content::SceneDocument& document,
                         const game::content::KitCatalog& catalog,
                         const std::string& assetRoot)
{
    if (mImpl->builtRevision == document.revision)
        return;
    mImpl->builtRevision = document.revision;
    mError.clear();

    // Rebuild from scratch. Wasteful per keystroke, and deliberately so for
    // now: a correct full rebuild is the baseline the incremental path has to
    // match, and the scenes this edits are hundreds of entities, not millions.
    mImpl->world.clear();
    mImpl->authorToNode.clear();
    mImpl->authorToHeight.clear();
    for (const game::content::Entity& entity : document.entities)
        mImpl->authorToHeight[entity.id] = entity.transform.position.y;
    entt::registry& registry = mImpl->world.registry();
    // Unresolved prefabs are reported, not fatal. Passing this vector is what
    // makes that true: without it buildRegistry abandons the registry on the
    // first bad prefab, and the whole level -- every wall, floor and light --
    // vanished from the viewport because one entity named a piece the kit no
    // longer had. The message below was the only clue, and it reads like a
    // note about one entity.
    std::vector<game::content::AuthorId> unresolved;
    if (!game::content::buildRegistry(document, catalog, registry, mError,
                                      &mImpl->authorToEntity, &unresolved,
                                      assetRoot)) {
        return;
    }
    if (!unresolved.empty()) {
        mError = std::to_string(unresolved.size()) +
                 (unresolved.size() == 1 ? " entity has" : " entities have") +
                 " an unresolved prefab (" + unresolved.front() +
                 (unresolved.size() > 1 ? ", ..." : "") +
                 ") -- shown as empty, everything else is drawn";
    }

    // MeshSource (a relative path) -> a loaded mesh handle. Same resolution the
    // runtime does, minus the absolute-path fallback: scenes are portable.
    auto view = registry.view<eng::ecs::MeshSource, eng::ecs::MeshRenderer>();
    for (const entt::entity entity : view) {
        const std::string& path = view.get<eng::ecs::MeshSource>(entity).path;
        view.get<eng::ecs::MeshRenderer>(entity).mesh = mImpl->meshFor(path);
    }
    // And the generated half, through the engine's own resolver rather than a
    // second copy of it -- the editor showing a different box from the one the
    // game builds is exactly the class of bug this preview exists to prevent.
    eng::ecs::resolvePrimitiveMeshes(registry, mImpl->renderer,
                                     mImpl->primitives);

    mImpl->world.sync();

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

    syncViewmodel(document);
}

// Places and equips the previewed hands from whichever entity carries a
// Viewmodel Preview component.
//
// One entity wins -- the first in document order. Two cameras both previewing
// hands would be two rigs in the viewport with no way to tell which is which,
// and the question the preview answers ("does this weapon sit right in the
// hand") is not one that gets clearer with a second answer on screen.
void PreviewBridge::syncViewmodel(const game::content::SceneDocument& document)
{
    const game::content::Entity* host = nullptr;
    for (const game::content::Entity& entity : document.entities) {
        if (entity.viewmodelPreview && entity.viewmodelPreview->visible) {
            host = &entity;
            break;
        }
    }
    if (!host || !mImpl->visible) {
        mImpl->hideViewmodel();
        return;
    }

    mImpl->loadViewmodelContent();
    if (!mImpl->viewmodelRoot.valid()) {
        mImpl->viewmodelRoot = mImpl->renderer.createNode(
            eng::kRootNode, glm::vec3(0.0f), "editor-viewmodel-preview");
        if (!mImpl->viewmodelRoot.valid())
            return;
    }
    // The authored camera's place in the world. The rig's own socket offset is
    // expressed in this node's space, exactly as it is against the head node in
    // the game, so what the editor frames is what the player will see.
    const game::content::XformAuthor& xf = host->transform;
    mImpl->renderer.setPosition(mImpl->viewmodelRoot, xf.position);
    mImpl->renderer.setOrientation(
        mImpl->viewmodelRoot, glm::quat(glm::radians(xf.rotationDegrees)));

    if (!mImpl->handsBuilt) {
        if (mImpl->handsFailed) {
            mError = "viewmodel preview: the cooked hand rig is unavailable";
            return;
        }
        mImpl->handsBuilt = mImpl->hands.init(
            mImpl->renderer, mImpl->viewmodelRoot, mImpl->handsDefinition);
        mImpl->builtWeapon.clear();
        if (!mImpl->handsBuilt) {
            mImpl->handsFailed = true;
            mError = "viewmodel preview: the cooked hand rig is unavailable";
            return;
        }
    }

    // The level's rig override if it authored one, otherwise the shipped
    // framing -- the same precedence the game applies on entering a level.
    mImpl->hands.setRig(host->viewmodelRig ? *host->viewmodelRig
                                           : game::ViewmodelRig{});

    const std::vector<game::PlayerWeaponDef>& defs = mImpl->weapons.defs();
    const game::PlayerWeaponDef* weapon = nullptr;
    if (!host->viewmodelPreview->weapon.empty())
        weapon = mImpl->weapons.find(host->viewmodelPreview->weapon);
    if (!weapon && !defs.empty())
        weapon = &defs.front(); // slot 0: what the player starts holding
    if (weapon && weapon->id != mImpl->builtWeapon) {
        mImpl->hands.setWeapon(mImpl->renderer, weapon->viewmodel, false);
        mImpl->builtWeapon = weapon->id;
    }

    mImpl->hands.applyPose(mImpl->renderer);
    mImpl->viewmodelHost = host->id;
    mImpl->renderer.setNodeVisible(mImpl->viewmodelRoot,
                                   !mImpl->cutAway(host->id));
    mImpl->viewmodelShown = true;
}

void PreviewBridge::tickViewmodel(float dt)
{
    if (!mImpl->viewmodelShown || !mImpl->handsBuilt)
        return;
    // Both channels get the frame delta here rather than the game's stepped
    // viewmodel rate: the editor is judging placement, and a 24 Hz clip in a
    // viewport reads as a dropped-frame editor rather than as the shipped look.
    mImpl->hands.update(mImpl->renderer, dt, dt, {});
}

void PreviewBridge::tickClips(float dt)
{
    // Nothing to do in a scene with no clips, which is nearly every scene. The
    // early-out is what keeps this off the editor's frame budget: sync()
    // reconciles the whole world, and paying for that every frame so that a
    // component almost nothing carries can animate is the wrong trade.
    if (mImpl->world.registry().view<eng::ecs::Clip>().empty())
        return;
    // The clip player writes component fields and tags moved transforms Dirty;
    // sync() is what turns that into node poses. Both, in that order, or a
    // scrub would change the components and nothing on screen.
    eng::ecs::clipSystem(mImpl->world, dt);
    mImpl->world.sync();
}

eng::ecs::World& PreviewBridge::world() { return mImpl->world; }

void PreviewBridge::setVisible(eng::Renderer& renderer, bool visible)
{
    for (const auto& [id, node] : mImpl->authorToNode)
        renderer.setNodeVisible(node, visible && !mImpl->cutAway(id));
    if (!visible)
        mImpl->hideGhost();
    mImpl->visible = visible;
}

void PreviewBridge::setIsolation(eng::Renderer& renderer, bool active,
                                 const std::vector<AuthorId>& members)
{
    // Compared before doing anything, like the hidden set: this is called every
    // frame, and re-walking every node per frame would cost more than the mode
    // saves.
    if (mImpl->isolating == active && mImpl->isolated.size() == members.size()) {
        bool same = true;
        for (const AuthorId& id : members)
            same = same && mImpl->isolated.count(id) != 0;
        if (same)
            return;
    }
    mImpl->isolating = active;
    mImpl->isolated.clear();
    for (const AuthorId& id : members)
        mImpl->isolated.insert(id);
    if (!mImpl->visible)
        return;
    for (const auto& [id, node] : mImpl->authorToNode)
        renderer.setNodeVisible(node, !mImpl->cutAway(id));
    // The previewed hands belong to whichever entity carries the component; if
    // that entity is outside the isolated subtree it goes with the rest.
    if (mImpl->viewmodelShown && mImpl->viewmodelRoot.valid())
        renderer.setNodeVisible(mImpl->viewmodelRoot,
                                !mImpl->isolating ||
                                    mImpl->isolated.count(
                                        mImpl->viewmodelHost) != 0);
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

// The ghost for a kit piece, parts and all.
//
// The scale is the piece's OWN import scale, not the catalogue's. That
// distinction is the bug this signature was changed for: the ghost was drawn at
// `catalog.scale()` (0.2 -- the kit's units-to-metres factor) for every piece,
// while a prop authored in metres declares `import_scale = 1.0` and is placed
// at that. Every imported model therefore previewed at a fifth of the size it
// landed at, and the wire box around it -- which did ask the piece -- was
// correct, so the ghost disagreed with its own outline.
void PreviewBridge::showPlacementGhost(
    const game::content::KitCatalog& catalog,
    const game::content::KitPiece& piece,
    const game::content::XformAuthor& transform)
{
    if (!mImpl->visible) {
        mImpl->hideGhost();
        return;
    }
    // Keyed on the piece rather than on its mesh: two pieces can share a mesh
    // and differ in what hangs off it.
    const std::string& key = piece.id;
    if (mImpl->visibleGhost != key)
        mImpl->hideGhost();

    auto found = mImpl->ghostNodes.find(key);
    if (found == mImpl->ghostNodes.end()) {
        Impl::GhostRig rig;
        rig.root = mImpl->renderer.createNode(eng::kRootNode, glm::vec3(0.0f),
                                              "editor_placement_ghost");
        // The parts, at the local offsets and local scales the cooker gives
        // their ECS children -- see SceneCook's attachment expansion, which
        // this mirrors deliberately.
        const auto attach = [&](auto&& self, eng::NodeHandle parent,
                                const game::content::KitPiece& current,
                                const glm::vec3& offsetFromRoot,
                                float scaleFromRoot) -> void {
            if (!current.isGroup()) {
                const eng::MeshHandle mesh = mImpl->meshFor(current.meshPath);
                if (mesh.valid()) {
                    mImpl->renderer.attachMesh(parent, mesh,
                                               "Editor/PlacementGhost", false);
                    eng::MeshBounds bounds;
                    if (mImpl->renderer.meshBounds(mesh, bounds)) {
                        const glm::vec3 min =
                            offsetFromRoot + bounds.min * scaleFromRoot;
                        const glm::vec3 max =
                            offsetFromRoot + bounds.max * scaleFromRoot;
                        rig.min = rig.hasBounds ? glm::min(rig.min, min) : min;
                        rig.max = rig.hasBounds ? glm::max(rig.max, max) : max;
                        rig.hasBounds = true;
                    }
                }
            }
            for (const game::content::KitAttachment& attachment :
                 current.attachments) {
                const game::content::KitPiece* part =
                    catalog.find(attachment.prefab);
                if (!part)
                    continue;
                const float partScale = part->meshScale(catalog.scale());
                const eng::NodeHandle node = mImpl->renderer.createNode(
                    parent, attachment.position, "editor_placement_ghost_part");
                mImpl->renderer.setScale(node, glm::vec3(partScale));
                rig.parts.push_back(node);
                self(self, node,
                     *part, offsetFromRoot + attachment.position * scaleFromRoot,
                     scaleFromRoot * partScale);
            }
        };
        attach(attach, rig.root, piece, glm::vec3(0.0f), 1.0f);
        found = mImpl->ghostNodes.emplace(key, std::move(rig)).first;
    }
    if (!found->second.hasBounds && found->second.parts.empty()) {
        // Nothing loaded -- a piece whose mesh is missing from the pack. The
        // wire box still has the catalogue's size to fall back on.
        mImpl->hideGhost();
        return;
    }

    mImpl->showRig(found->second, transform,
                   piece.meshScale(catalog.scale()));
    mImpl->visibleGhost = key;
}

// The ghost, for the two brushes that are not kit pieces.
//
// `key` is what the rig is cached under; it is the mesh path for a file and a
// synthetic description for a primitive, because two boxes of different sizes
// are two ghosts and two entities naming one .obj are one.
void PreviewBridge::showGhostMesh(const std::string& key, eng::MeshHandle mesh,
                                  const game::content::XformAuthor& transform,
                                  float importScale)
{
    if (!mImpl->visible || !mesh.valid()) {
        mImpl->hideGhost();
        return;
    }

    if (mImpl->visibleGhost != key)
        mImpl->hideGhost();

    auto found = mImpl->ghostNodes.find(key);
    if (found == mImpl->ghostNodes.end()) {
        Impl::GhostRig rig;
        rig.root = mImpl->renderer.createNode(eng::kRootNode, glm::vec3(0.0f),
                                              "editor_placement_ghost");
        mImpl->renderer.attachMesh(rig.root, mesh, "Editor/PlacementGhost",
                                   false);
        eng::MeshBounds bounds;
        if (mImpl->renderer.meshBounds(mesh, bounds)) {
            rig.min = bounds.min;
            rig.max = bounds.max;
            rig.hasBounds = true;
        }
        found = mImpl->ghostNodes.emplace(key, std::move(rig)).first;
    }

    mImpl->showRig(found->second, transform, importScale);
    mImpl->visibleGhost = key;
}

void PreviewBridge::showMeshPlacementGhost(
    const std::string& meshPath, const game::content::XformAuthor& transform,
    float importScale)
{
    if (meshPath.empty()) {
        mImpl->hideGhost();
        return;
    }
    showGhostMesh(meshPath, mImpl->meshFor(meshPath), transform, importScale);
}

void PreviewBridge::showPrimitivePlacementGhost(
    const eng::ecs::PrimitiveMesh& primitive,
    const game::content::XformAuthor& transform)
{
    showGhostMesh(primitiveGhostKey(primitive),
                  mImpl->primitives.get(mImpl->renderer, primitive), transform,
                  1.0f);
}

namespace {
// Shared by both offsets below: the lift that puts `bounds.min.y` at zero.
float baseLift(const eng::MeshBounds& bounds, float scale)
{
    // Never pushes anything DOWN. A mesh whose geometry starts above its own
    // origin (a hanging lamp, a ceiling boss) is authored that way on purpose,
    // and dropping it to the floor would be a worse bug than the one this
    // fixes.
    return bounds.min.y < 0.0f ? -bounds.min.y * scale : 0.0f;
}
} // namespace

float PreviewBridge::meshBaseOffset(const std::string& meshPath,
                                    float importScale) const
{
    if (meshPath.empty())
        return 0.0f;
    // meshFor caches, so this is the same handle the ghost draws -- the
    // preview and the committed entity cannot disagree about where the mesh
    // sits, which is the property that made this bug so confusing to report.
    const eng::MeshHandle mesh = mImpl->meshFor(meshPath);
    eng::MeshBounds bounds;
    if (!mesh.valid() || !mImpl->renderer.meshBounds(mesh, bounds))
        return 0.0f;
    return baseLift(bounds, importScale > 0.0f ? importScale : 1.0f);
}

float PreviewBridge::primitiveBaseOffset(
    const eng::ecs::PrimitiveMesh& primitive) const
{
    const eng::MeshHandle mesh =
        mImpl->primitives.get(mImpl->renderer, primitive);
    eng::MeshBounds bounds;
    if (!mesh.valid() || !mImpl->renderer.meshBounds(mesh, bounds))
        return 0.0f;
    return baseLift(bounds, 1.0f);
}

bool PreviewBridge::ghostBounds(glm::vec3& min, glm::vec3& max) const
{
    if (mImpl->visibleGhost.empty())
        return false;
    const auto found = mImpl->ghostNodes.find(mImpl->visibleGhost);
    if (found == mImpl->ghostNodes.end() || !found->second.hasBounds)
        return false;
    // The union the rig recorded when it was built: for a compound piece that
    // is the whole object, sword included, which is what an author is judging
    // when they ask whether it fits.
    min = found->second.min;
    max = found->second.max;
    return true;
}

std::string primitiveGhostKey(const eng::ecs::PrimitiveMesh& p)
{
    // Not a hash: a collision here would silently show the wrong ghost, and the
    // string is built once per frame at most. The prefix keeps it out of the
    // mesh paths sharing the same map.
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer),
                  "primitive:%s:%g,%g,%g:%g:%g:%g:%g:%d:%d:%d:%d",
                  eng::ecs::primitiveKindName(p.kind), double(p.size.x),
                  double(p.size.y), double(p.size.z), double(p.radius),
                  double(p.height), double(p.bevel), double(p.thickness),
                  p.rings, p.segments, p.subdivisions, p.inwardFacing ? 1 : 0);
    return buffer;
}

void PreviewBridge::showParticlePlacementGhost(
    const std::string& effect, const game::content::XformAuthor& transform,
    float sizeScale, float repeatSeconds)
{
    if (!mImpl->visible || effect.empty()) {
        mImpl->hideGhost();
        return;
    }

    // A brush changes kind atomically: no mesh ghost should remain under the
    // live particle preview.
    if (!mImpl->visibleGhost.empty()) {
        const auto found = mImpl->ghostNodes.find(mImpl->visibleGhost);
        if (found != mImpl->ghostNodes.end())
            mImpl->renderer.setNodeVisible(found->second.root, false);
        mImpl->visibleGhost.clear();
    }
    if (!mImpl->particleGhostNode.valid())
        mImpl->particleGhostNode = mImpl->renderer.createNode(
            eng::kRootNode, glm::vec3(0.0f), "editor_particle_brush");

    mImpl->renderer.setPosition(mImpl->particleGhostNode, transform.position);
    mImpl->renderer.setOrientation(
        mImpl->particleGhostNode,
        game::content::authorOrientation(transform.rotationDegrees));
    mImpl->renderer.setScale(mImpl->particleGhostNode, transform.scale);

    const auto now = std::chrono::steady_clock::now();
    const bool repeat = repeatSeconds > 0.0f &&
                        now >= mImpl->particleGhostRestart;
    if (mImpl->particleGhostEffect != effect ||
        !mImpl->particleGhost.valid() || repeat) {
        if (mImpl->particleGhost.valid())
            mImpl->renderer.despawnParticles(mImpl->particleGhost);
        eng::ParticleSpawnOptions options;
        options.sizeScale = sizeScale;
        mImpl->particleGhost = mImpl->renderer.spawnParticles(
            effect, mImpl->particleGhostNode, options);
        mImpl->particleGhostEffect = effect;
        mImpl->particleGhostRestart =
            repeatSeconds > 0.0f
                ? now + std::chrono::duration_cast<
                            std::chrono::steady_clock::duration>(
                            std::chrono::duration<float>(repeatSeconds))
                : std::chrono::steady_clock::time_point::max();
    }
}

void PreviewBridge::hidePlacementGhost()
{
    mImpl->hideGhost();
}

void PreviewBridge::setHiddenEntities(eng::Renderer& renderer,
                                      const std::vector<AuthorId>& hidden)
{
    // Compared before doing anything: this is called every frame from the
    // panel, and re-walking every node per frame would cost more than the
    // feature saves.
    if (mImpl->hidden.size() == hidden.size()) {
        bool same = true;
        for (const AuthorId& id : hidden)
            same = same && mImpl->hidden.count(id) != 0;
        if (same)
            return;
    }
    mImpl->hidden.clear();
    mImpl->hidden.insert(hidden.begin(), hidden.end());
    for (const auto& [id, node] : mImpl->authorToNode)
        renderer.setNodeVisible(node, mImpl->visible && !mImpl->cutAway(id));
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
