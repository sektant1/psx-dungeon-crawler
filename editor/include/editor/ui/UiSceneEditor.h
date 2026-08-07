#pragma once
#include <editor/content/SceneDocument.h>

#include <eng/ui/UiScene.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eng::ui { class UiCanvas; }

namespace ed {

// Editing screen-space UI in the 2D viewport.
//
// The 2D viewport used to be a preview: it drew the game's code-authored HUD
// against dialled-in state and said in its own header that it was "not a WYSIWYG
// layout editor". It could not be one, because there was nothing to edit -- the
// HUD is C++.
//
// Now a screen is a scene, so there is. This module is what turns the panel into
// an editor: it builds the document's UI entities into a registry, solves them
// with the *same* `eng::ui::solveUiLayout` the game paints with, hit-tests a
// click against the result, and turns a drag on a handle back into the offsets
// the document stores.
//
// Using the shipped solver rather than a preview copy is the whole point. A
// viewport that laid boxes out its own way would be a second implementation of
// the one rule an author is trying to learn, and the two would disagree the
// first time either changed.
class UiSceneEditor {
public:
    // What the author grabbed. Corners resize both axes, edges one, and the
    // body moves -- the arrangement every 2D editor uses, so nobody has to be
    // told.
    enum class Handle {
        None,
        Body,
        Left, Right, Top, Bottom,
        TopLeft, TopRight, BottomLeft, BottomRight
    };

    // Rebuild from the document and solve against a surface of `virtualSize`.
    // Cheap enough to run every frame: a screen is tens of entities, and a
    // cache would have to be invalidated by every inspector edit.
    // `assetRoot` is what atom placements resolve against. Instances are
    // expanded here rather than by the caller because a UI scene built from the
    // atom library is *mostly* placements: without expansion the viewport shows
    // the boxes and none of their contents, which looks exactly like the atoms
    // failing to draw.
    void rebuild(const game::content::SceneDocument& document,
                 glm::ivec2 virtualSize, const std::string& assetRoot = {});

    // Paint what was solved, so the viewport shows the screen as the game will.
    // `data` may be null, which is the editor's normal case -- an unanswered
    // binding falls back to the authored text, which is what makes a screen
    // previewable with no game behind it.
    void paint(eng::ui::UiCanvas& canvas,
               const eng::ui::UiDataSource* data = nullptr) const;

    // Topmost UI entity under a virtual-pixel point, or empty.
    game::content::AuthorId pick(glm::ivec2 point) const;

    // The solved box of one authored entity, for drawing handles. Returns false
    // when it is not a UI entity or is hidden.
    bool boundsOf(const game::content::AuthorId& id,
                  eng::ui::UiRect& out) const;

    // Which handle a point is on, given the selection's solved box. `grabPixels`
    // is the handle's reach in virtual pixels -- scaled by the caller so the
    // grab area is a constant number of *screen* pixels however far the view is
    // zoomed, which is what makes a handle grabbable at 1x and at 8x.
    static Handle handleAt(const eng::ui::UiRect& bounds, glm::ivec2 point,
                           int grabPixels);

    // Apply a drag, in virtual pixels, to an entity's rect.
    //
    // Writes offsets only, never anchors: an anchor is a layout *decision* the
    // author made and a drag must not silently change it. Dragging a
    // stretched box therefore adjusts its margins, which is what "move this
    // 4px right" means for a box pinned to both edges.
    //
    // Returns false when nothing changed, so a caller can avoid recording an
    // undo entry for a drag that moved zero pixels.
    static bool applyDrag(game::content::Entity& entity, Handle handle,
                          glm::ivec2 deltaPixels, int minimumSize = 4);

private:
    struct Solved {
        game::content::AuthorId id;
        eng::ui::UiRect bounds;
    };

    // The registry is rebuilt rather than kept in step with the document. An
    // incremental mirror would be a second source of truth for what a screen
    // contains, and the whole failure mode of editor code is two of those
    // disagreeing.
    entt::registry mRegistry;
    std::vector<eng::ui::UiSolvedRect> mSolved;
    std::vector<Solved> mByAuthor;
};

} // namespace ed
