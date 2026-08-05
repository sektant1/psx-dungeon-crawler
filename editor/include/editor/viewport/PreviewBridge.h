#pragma once
#include <editor/content/KitCatalog.h>
#include <editor/content/SceneDocument.h>

#include <eng/Handles.h>
#include <eng/ecs/MeshResolve.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/PrimitiveMesh.h>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace eng {
class Renderer;
}

namespace ed {

// Shows the authored document in the viewport.
//
// The preview is built by the SAME expansion the cooker uses
// (game::content::buildRegistry), so what the editor draws is what the map will
// contain -- there is no second interpretation of what "kit.wall" means.
// Everything it owns is transient and derived: nothing here is ever saved, and
// nothing here is ever referenced by an undo command (which is why commands
// address entities by AuthorId, never by the handles this hands out).
class PreviewBridge
{
public:
    explicit PreviewBridge(eng::Renderer& renderer);
    ~PreviewBridge();

    PreviewBridge(const PreviewBridge&) = delete;
    PreviewBridge& operator=(const PreviewBridge&) = delete;

    // Rebuilds the preview when the document has moved on. Cheap to call every
    // frame: it compares the document's revision and returns immediately when
    // nothing changed.
    void sync(const game::content::SceneDocument& document,
              const game::content::KitCatalog& catalog);
    // Advances the first-person hands shown by a Viewmodel Preview component:
    // the authored clip, and the procedural placement when the rig has motion
    // enabled. Separate from sync() because the document has not changed --
    // the rig is simply animating, once per frame.
    void tickViewmodel(float dt);
    // Forces the next sync to rebuild, after an undo/redo or a fresh load.
    void invalidate();
    // Hides the whole level without tearing it down, for the modes that need
    // the viewport to themselves.
    void setVisible(eng::Renderer& renderer, bool visible);
    // Hides everything standing above `height` metres. A dungeon with ceilings
    // is a closed box, and the editor looks into it from above: without a cut
    // the top-down view is a lid. Cheap to call every frame -- it returns
    // immediately unless the height moved.
    void setCeilingCut(eng::Renderer& renderer, float height);
    // Shows only `members` -- an entity and its descendants -- and hides the
    // level around them. `active` false restores the whole scene. Cheap to call
    // every frame; it returns immediately unless the set moved.
    void setIsolation(eng::Renderer& renderer, bool active,
                      const std::vector<game::content::AuthorId>& members);

    // Transient brush mesh under the cursor. It never enters the document or
    // ECS preview, so it cannot be picked, cooked or recorded by undo.
    //
    // A kit piece is shown WHOLE: its own mesh plus every attachment it
    // declares, at the offsets the cooker will use. A compound piece used to
    // ghost as its root alone -- a boss with no sword -- and a mesh-less group
    // (the root of an imported multi-part model) ghosted as nothing at all.
    //
    // The catalogue comes in because the piece's parts are named, not owned,
    // and because a piece's own `import_scale` beats the kit's.
    void showPlacementGhost(const game::content::KitCatalog& catalog,
                            const game::content::KitPiece& piece,
                            const game::content::XformAuthor& transform);
    // The same, for the two brushes that are not kit pieces. A mesh file and a
    // generated primitive are as placeable as a wall is, and the ghost is what
    // makes placing anything judgeable before the click rather than after it.
    void showMeshPlacementGhost(const std::string& meshPath,
                                const game::content::XformAuthor& transform,
                                float importScale);
    void showPrimitivePlacementGhost(
        const eng::ecs::PrimitiveMesh& primitive,
        const game::content::XformAuthor& transform);
    // Local bounds of whatever the ghost is currently showing, for the wire box
    // the viewport draws around it. Asked of the renderer rather than derived
    // from a kit piece, because two of the three brushes have no kit piece.
    bool ghostBounds(glm::vec3& min, glm::vec3& max) const;

    // How far to lift this mesh so its base sits on a surface, in metres.
    //
    // -min.y of the mesh's own bounds, scaled. Zero for a mesh authored with
    // its base at the origin, which is why applying it unconditionally cannot
    // move anything that was already placed correctly; it only rescues the
    // centre-authored meshes that used to sink halfway into the floor.
    float meshBaseOffset(const std::string& meshPath, float importScale) const;
    float primitiveBaseOffset(const eng::ecs::PrimitiveMesh& primitive) const;
    // A real live effect at the brush position. A wire box cannot communicate
    // spread, direction, lifetime, or scale, which are the values an author is
    // choosing when placing particles.
    void showParticlePlacementGhost(
        const std::string& effect,
        const game::content::XformAuthor& transform, float sizeScale = 1.0f,
        float repeatSeconds = 0.0f);
    void hidePlacementGhost();

    // Same storey/whole-preview visibility decision used by rendering. Picking
    // must not select geometry hidden by the ceiling cut.
    bool entityVisible(const game::content::AuthorId& id) const;

    // Entities the author has switched off in the outliner. Held here rather
    // than asked for per node so the decision stays with the rest of the
    // visibility rules -- the ceiling cut and the whole-preview toggle already
    // live here, and three places deciding what is drawn is how one of them
    // ends up disagreeing.
    void setHiddenEntities(eng::Renderer& renderer,
                           const std::vector<game::content::AuthorId>& hidden);

    // Null when the entity has no visual (a marker, or the document moved on).
    const eng::NodeHandle* nodeFor(const game::content::AuthorId& id) const;
    const std::string& lastError() const { return mError; }

private:
    struct Impl;

    void syncViewmodel(const game::content::SceneDocument& document);
    void showGhostMesh(const std::string& key, eng::MeshHandle mesh,
                       const game::content::XformAuthor& transform,
                       float importScale);

    std::unique_ptr<Impl> mImpl;
    std::string mError;
};

// The cache key a generated mesh's ghost node is stored under: everything that
// changes its geometry, in one string. Exposed for the test that checks two
// different boxes do not share a ghost.
std::string primitiveGhostKey(const eng::ecs::PrimitiveMesh& primitive);

} // namespace ed
