// Authoring screen-space UI in the 2D viewport.
//
// The rules here are the ones an author feels immediately and cannot work
// around: grabbing a corner must not resize one axis, dragging must never flip
// a box inside out, and a drag must move offsets rather than quietly rewriting
// the anchors the author chose.
#include <editor/ui/UiSceneEditor.h>

#include <cstdio>

using ed::UiSceneEditor;
using game::content::Entity;
using game::content::SceneDocument;
using game::content::UiAuthor;
using Handle = ed::UiSceneEditor::Handle;

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}

namespace {

eng::ui::UiRect box(int x, int y, int w, int h)
{
    eng::ui::UiRect r;
    r.position = {x, y};
    r.size = {w, h};
    return r;
}

// A fixed-size element: equal anchors, so offsets are pixels from one point.
Entity pinned(const char* id, glm::vec2 min, glm::vec2 max)
{
    Entity e;
    e.id = id;
    UiAuthor ui;
    ui.rect.anchorMin = ui.rect.anchorMax = {0.0f, 0.0f};
    ui.rect.offsetMin = min;
    ui.rect.offsetMax = max;
    ui.panel = eng::ecs::UiPanel{};
    e.ui = ui;
    return e;
}

} // namespace

int main()
{
    // --- which handle a point is on ------------------------------------------
    {
        const eng::ui::UiRect bounds = box(20, 30, 100, 40);
        check(UiSceneEditor::handleAt(bounds, {20, 30}, 3) == Handle::TopLeft,
              "the top-left corner");
        check(UiSceneEditor::handleAt(bounds, {120, 70}, 3) == Handle::BottomRight,
              "the bottom-right corner");
        check(UiSceneEditor::handleAt(bounds, {120, 30}, 3) == Handle::TopRight,
              "the top-right corner");
        check(UiSceneEditor::handleAt(bounds, {20, 70}, 3) == Handle::BottomLeft,
              "the bottom-left corner");
        // Corners must win over edges: at (20,30) both the left and top tests
        // pass, and answering Left there resizes one axis when the author
        // grabbed two.
        check(UiSceneEditor::handleAt(bounds, {20, 50}, 3) == Handle::Left,
              "a point on the left edge but not a corner");
        check(UiSceneEditor::handleAt(bounds, {70, 30}, 3) == Handle::Top,
              "the top edge");
        check(UiSceneEditor::handleAt(bounds, {70, 50}, 3) == Handle::Body,
              "the middle moves");
        check(UiSceneEditor::handleAt(bounds, {200, 200}, 3) == Handle::None,
              "far away is nothing");
        // Reachable from outside: a 6px-tall label has no inside to grab.
        check(UiSceneEditor::handleAt(bounds, {18, 30}, 3) == Handle::TopLeft,
              "handles are grabbable from just outside the box");
    }

    // --- moving ---------------------------------------------------------------
    {
        Entity e = pinned("a", {10.0f, 10.0f}, {110.0f, 34.0f});
        check(UiSceneEditor::applyDrag(e, Handle::Body, {5, -3}),
              "the drag changed something");
        check(e.ui->rect.offsetMin == glm::vec2(15.0f, 7.0f),
              "the near corner moved");
        check(e.ui->rect.offsetMax == glm::vec2(115.0f, 31.0f),
              "and the far one moved with it, keeping the size");
        // The anchors are a decision, not a consequence.
        check(e.ui->rect.anchorMin == glm::vec2(0.0f, 0.0f) &&
                  e.ui->rect.anchorMax == glm::vec2(0.0f, 0.0f),
              "a move never touches the anchors");
    }

    // --- resizing from each side ---------------------------------------------
    {
        Entity e = pinned("a", {10.0f, 10.0f}, {110.0f, 34.0f});
        UiSceneEditor::applyDrag(e, Handle::Right, {20, 0});
        check(e.ui->rect.offsetMax.x == 130.0f, "the right edge moved out");
        check(e.ui->rect.offsetMin.x == 10.0f, "the left edge stayed");

        UiSceneEditor::applyDrag(e, Handle::Top, {0, 4});
        check(e.ui->rect.offsetMin.y == 14.0f, "the top edge moved down");
        check(e.ui->rect.offsetMax.y == 34.0f, "the bottom stayed");
    }

    // --- a resize cannot invert the box --------------------------------------
    //
    // Dragging the left edge far past the right one must park at the minimum,
    // not produce a negative size -- which the solver would normalise to zero
    // and the author would experience as the widget vanishing.
    {
        Entity e = pinned("a", {10.0f, 10.0f}, {110.0f, 34.0f});
        UiSceneEditor::applyDrag(e, Handle::Left, {500, 0}, /*minimumSize=*/4);
        check(e.ui->rect.offsetMax.x - e.ui->rect.offsetMin.x >= 4.0f,
              "width clamped to the minimum");
        check(e.ui->rect.offsetMax.x == 110.0f,
              "and the edge that was not dragged did not move");

        UiSceneEditor::applyDrag(e, Handle::Bottom, {0, -500}, 4);
        check(e.ui->rect.offsetMax.y - e.ui->rect.offsetMin.y >= 4.0f,
              "height clamped too");
    }

    // A zero drag reports no change, so the caller records no undo entry for a
    // click that did not move anything.
    {
        Entity e = pinned("a", {10.0f, 10.0f}, {110.0f, 34.0f});
        check(!UiSceneEditor::applyDrag(e, Handle::Body, {0, 0}),
              "a zero drag changes nothing");
        check(!UiSceneEditor::applyDrag(e, Handle::None, {5, 5}),
              "and neither does dragging no handle");
    }

    // --- picking through the document ----------------------------------------
    {
        SceneDocument document;
        document.entities.push_back(pinned("plate", {0.0f, 0.0f}, {100.0f, 100.0f}));
        Entity label = pinned("label", {10.0f, 10.0f}, {60.0f, 24.0f});
        label.parent = "plate";
        // Relative to the plate now, so the same offsets mean (10,10) inside it.
        document.entities.push_back(label);

        UiSceneEditor editor;
        editor.rebuild(document, {320, 240});
        check(editor.pick({20, 15}) == "label",
              "the child on top is picked");
        check(editor.pick({80, 80}) == "plate",
              "the plate where the child is not");
        check(editor.pick({200, 200}).empty(), "empty space picks nothing");

        eng::ui::UiRect bounds;
        check(editor.boundsOf("label", bounds), "the label has a solved box");
        check(bounds.position == glm::ivec2(10, 10),
              "resolved against its parent, not the screen");
    }

    // A hidden element is not pickable: the editor must not offer a handle on
    // something the author cannot see.
    {
        SceneDocument document;
        Entity plate = pinned("plate", {0.0f, 0.0f}, {100.0f, 100.0f});
        plate.ui->rect.visible = false;
        document.entities.push_back(plate);

        UiSceneEditor editor;
        editor.rebuild(document, {320, 240});
        check(editor.pick({50, 50}).empty(), "a hidden element is not pickable");
        eng::ui::UiRect bounds;
        check(!editor.boundsOf("plate", bounds), "and has no handle box");
    }

    if (failures == 0)
        std::printf("UiSceneEditorTests OK\n");
    return failures == 0 ? 0 : 1;
}
