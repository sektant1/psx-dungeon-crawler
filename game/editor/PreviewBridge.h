#pragma once
#include "KitCatalog.h"
#include "SceneDocument.h"

#include <eng/Handles.h>
#include <eng/ecs/Scene.h>
#include <eng/ecs/SceneSync.h>

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
    PreviewBridge(eng::Renderer& renderer, const std::string& assetRoot);
    ~PreviewBridge();

    PreviewBridge(const PreviewBridge&) = delete;
    PreviewBridge& operator=(const PreviewBridge&) = delete;

    // Rebuilds the preview when the document has moved on. Cheap to call every
    // frame: it compares the document's revision and returns immediately when
    // nothing changed.
    void sync(const game::content::SceneDocument& document,
              const game::content::KitCatalog& catalog);
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

    // Transient brush mesh under the cursor. It never enters the document or
    // ECS preview, so it cannot be picked, cooked or recorded by undo.
    void showPlacementGhost(const game::content::KitPiece& piece,
                            const game::content::XformAuthor& transform,
                            float importScale);
    void hidePlacementGhost();

    // Same storey/whole-preview visibility decision used by rendering. Picking
    // must not select geometry hidden by the ceiling cut.
    bool entityVisible(const game::content::AuthorId& id) const;

    // Null when the entity has no visual (a marker, or the document moved on).
    const eng::NodeHandle* nodeFor(const game::content::AuthorId& id) const;
    const std::string& lastError() const { return mError; }

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    std::string mError;
};

} // namespace ed
