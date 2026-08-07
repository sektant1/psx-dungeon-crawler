#pragma once
#include <eng/ui/UiLayout.h>

// The whole header, not a forward declaration: `entt::basic_registry<entt::entity>`
// is what a caller passes and a view over it is what the implementation needs,
// neither of which survives a forward declaration.
#include <entt/entt.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace eng::ui {

class UiCanvas;

// Where an authored screen gets its live values.
//
// A screen is a scene, so what it *says* has to come from somewhere the scene
// does not know about: the player's weight, the shop's prices, the lines of a
// conversation. The alternative -- a script writing text into UiLabel
// components every frame -- would work and is worse: it puts a per-frame string
// copy in Lua, and it makes the editor's preview show whatever the last run
// left behind.
//
// So the scene names a key and the game answers it. Unknown keys are not an
// error: an unanswered binding falls back to the authored value, which is
// exactly what makes the same scene previewable in an editor that has no game
// behind it.
class UiDataSource {
public:
    virtual ~UiDataSource() = default;

    // A number for a UiBar's ratio or a UiLabel's text. Return false to leave
    // the authored value in place.
    virtual bool number(std::string_view key, float& out) const
    {
        (void)key; (void)out;
        return false;
    }
    virtual bool text(std::string_view key, std::string& out) const
    {
        (void)key; (void)out;
        return false;
    }

    // One line of a UiList.
    struct Row {
        std::string label;
        std::string value;   // right column; empty draws nothing
        // Negative means "no gauge". A row with one is drawn as a thin bar
        // under the text, which is how condition and stack fullness read
        // without costing a column.
        float ratio = -1.0f;
        bool selected = false;
        bool dim = false;    // owned-but-unavailable, unaffordable, locked
    };
    // Fill `out` with up to `max` rows. Returning 0 draws an empty list, which
    // is a real state and looks different from a missing one.
    virtual int rows(std::string_view key, int max, std::vector<Row>& out) const
    {
        (void)key; (void)max; (void)out;
        return 0;
    }
};

// One entity's resolved box, in virtual pixels. Produced by layout, consumed by
// paint -- and by the editor, which needs the same rectangles to hit-test a
// click and draw a handle exactly where the widget is.
struct UiSolvedRect {
    // Always assigned by the solver, so this default is never read -- but it is
    // entt::null rather than a default-constructed entity for the reason
    // UiListHit gives below: `entt::entity{}` is entity zero, and leaving that
    // trap set for the next person costs nothing to avoid.
    entt::entity entity = entt::null;
    UiRect bounds;
    int depth = 0; // hierarchy depth, for stable painter's ordering
};

// Resolve every visible UiRect in the registry against `surface`, in paint
// order (parents before children, `order` then scene order among siblings).
//
// Hidden entities and their whole subtree are omitted rather than marked, so a
// caller cannot accidentally paint or hit-test an invisible screen by ignoring
// a flag.
void solveUiLayout(const entt::basic_registry<entt::entity>& registry,
                   UiRect surface, std::vector<UiSolvedRect>& out);

// Paint a solved layout. Split from solving because the editor needs the
// rectangles without the pixels (to hit-test) and the game needs the pixels
// without keeping the rectangles.
void paintUiScene(UiCanvas& canvas,
                  const entt::basic_registry<entt::entity>& registry,
                  const std::vector<UiSolvedRect>& solved,
                  const UiDataSource* data);

// Solve and paint over the canvas's whole surface. The one call a game makes.
void drawUiScene(UiCanvas& canvas,
                 const entt::basic_registry<entt::entity>& registry,
                 const UiDataSource* data);

// Topmost entity whose solved rect contains `point`, or entt::null. The
// editor's picker: last in paint order wins, because that is what the user sees
// on top.
entt::entity pickUi(const std::vector<UiSolvedRect>& solved, glm::ivec2 point);

// Which row of a list a point is on, or -1.
//
// Mirrors the row pitch paintUiScene uses, and shares the one line that decides
// it: a hit test computing its own pitch would drift from the paint the first
// time either changed, and the symptom -- clicking one row and selecting its
// neighbour -- is maddening to diagnose.
int rowAt(const UiRect& listBounds, float rowHeight, float rowGap,
          glm::ivec2 point);

// The topmost UiList under a point, with the row. Returns entt::null when the
// point is not over a list.
struct UiListHit {
    // entt::null, not a default-constructed entity: `entt::entity{}` is entity
    // *zero*, which is a perfectly valid one. A miss returning it would have
    // the caller act on whatever entity 0 happens to be.
    entt::entity entity = entt::null;
    int row = -1;
};
UiListHit pickUiRow(const entt::basic_registry<entt::entity>& registry,
                    const std::vector<UiSolvedRect>& solved, glm::ivec2 point);

} // namespace eng::ui
