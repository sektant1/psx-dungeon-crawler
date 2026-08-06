// Rubber-band selection and click cycling: Gregory §15.4.1.4.
//
// Cycling is state whose bugs only appear on the second and third click, which
// is exactly the kind of thing nobody catches by hand. The band is screen-space
// maths against a view-projection, so both projections are exercised here
// without a window.

#include <editor/scene/SelectionTools.h>

#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <string>

using namespace game::content;
namespace selection = ed::selection;

static int gFailures = 0;

static void check(bool condition, const std::string& what)
{
    if (!condition) {
        std::cerr << "EditorSelectionTests: " << what << '\n';
        ++gFailures;
    }
}

static selection::Candidate at(const std::string& id, glm::vec3 centre,
                               float half = 0.5f)
{
    selection::Candidate candidate;
    candidate.id = id;
    candidate.boundsMin = centre - glm::vec3(half);
    candidate.boundsMax = centre + glm::vec3(half);
    return candidate;
}

// A plan view looking straight down at the origin, spanning 20 metres. The
// same matrix the editor's own elevation builds.
static glm::mat4 topDownViewProjection()
{
    const glm::vec3 eye{0.0f, 50.0f, 0.0f};
    const glm::mat4 view =
        glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::mat4 projection =
        glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.05f, 4000.0f);
    return projection * view;
}

static void testRectNormalises()
{
    // Dragged up and to the left: the rectangle must still be well formed.
    const selection::ScreenRect rect =
        selection::ScreenRect::fromCorners({100.0f, 100.0f}, {40.0f, 20.0f});
    check(rect.min.x == 40.0f && rect.min.y == 20.0f, "min is the low corner");
    check(rect.max.x == 100.0f && rect.max.y == 100.0f,
          "max is the high corner");
    check(rect.contains({50.0f, 30.0f}), "a point inside is inside");
    check(!rect.contains({10.0f, 30.0f}), "a point outside is outside");
    check(!rect.degenerate(), "a real drag is not degenerate");

    const selection::ScreenRect tiny =
        selection::ScreenRect::fromCorners({10.0f, 10.0f}, {11.0f, 12.0f});
    check(tiny.degenerate(), "a one-pixel wobble is a click, not a band");
}

static void testMarqueeCatchesWhatItCrosses()
{
    // A 200x200 viewport showing 20 world metres: 10 pixels per metre, world
    // origin at the centre of the viewport.
    const glm::vec2 origin{0.0f, 0.0f};
    const glm::vec2 size{200.0f, 200.0f};
    const glm::mat4 vp = topDownViewProjection();

    const std::vector<selection::Candidate> candidates = {
        at("centre", {0.0f, 0.0f, 0.0f}),
        at("far", {8.0f, 0.0f, 0.0f}),
    };

    // A band over the middle of the screen catches only the entity there.
    const selection::ScreenRect middle =
        selection::ScreenRect::fromCorners({80.0f, 80.0f}, {120.0f, 120.0f});
    const std::vector<AuthorId> hits =
        selection::marquee(candidates, vp, origin, size, middle);
    check(hits.size() == 1 && hits.front() == "centre",
          "the band caught the entity it was drawn over");

    // A band over the whole viewport catches everything.
    const selection::ScreenRect all =
        selection::ScreenRect::fromCorners({0.0f, 0.0f}, {200.0f, 200.0f});
    check(selection::marquee(candidates, vp, origin, size, all).size() == 2,
          "a band over everything catches everything");

    // And one over empty space catches nothing.
    const selection::ScreenRect empty =
        selection::ScreenRect::fromCorners({190.0f, 0.0f}, {200.0f, 10.0f});
    check(selection::marquee(candidates, vp, origin, size, empty).empty(),
          "a band over nothing catches nothing");
}

// Touch takes anything the band crosses; Enclose demands the whole footprint.
// The difference is what makes it possible to box one room without dragging in
// the corridor behind it.
static void testEncloseIsStricterThanTouch()
{
    const glm::vec2 origin{0.0f, 0.0f};
    const glm::vec2 size{200.0f, 200.0f};
    const glm::mat4 vp = topDownViewProjection();

    // A two-metre box at the origin spans 20 pixels, centred at (100, 100).
    const std::vector<selection::Candidate> candidates = {
        at("wide", {0.0f, 0.0f, 0.0f}, 1.0f),
    };
    // A band clipping only its left half.
    const selection::ScreenRect half =
        selection::ScreenRect::fromCorners({80.0f, 80.0f}, {100.0f, 120.0f});

    check(selection::marquee(candidates, vp, origin, size, half,
                             selection::Fit::Touch)
              .size() == 1,
          "Touch catches a partly covered entity");
    check(selection::marquee(candidates, vp, origin, size, half,
                             selection::Fit::Enclose)
              .empty(),
          "Enclose does not");
}

static void testPickCycleWalksThenWraps()
{
    selection::PickCycle cycle;
    const std::vector<AuthorId> stack = {"barrel", "crate", "room"};
    const glm::vec2 point{100.0f, 100.0f};

    check(cycle.next(stack, point) == "barrel",
          "the first click takes the nearest");
    check(cycle.depth() == 0 && cycle.count() == 3, "depth and count reported");
    check(cycle.next(stack, point) == "crate", "the second goes one deeper");
    check(cycle.next(stack, point) == "room", "the third goes deeper again");
    check(cycle.next(stack, point) == "barrel", "the fourth wraps to the top");
}

static void testPickCycleRestartsWhenTheClickMoves()
{
    selection::PickCycle cycle;
    const std::vector<AuthorId> stack = {"barrel", "crate"};

    check(cycle.next(stack, {100.0f, 100.0f}) == "barrel", "first click");
    check(cycle.next(stack, {100.0f, 100.0f}) == "crate", "second click");
    // Well away from the last one: the author has moved on.
    check(cycle.next(stack, {400.0f, 400.0f}) == "barrel",
          "a click somewhere else starts from the nearest again");

    // Same spot, but the camera moved and the stack under it changed.
    const std::vector<AuthorId> other = {"wall", "floor"};
    check(cycle.next(other, {400.0f, 400.0f}) == "wall",
          "a different stack at the same spot also restarts");

    cycle.reset();
    check(cycle.next(stack, {400.0f, 400.0f}) == "barrel",
          "an explicit reset starts over");

    check(cycle.next({}, {0.0f, 0.0f}).empty(),
          "an empty stack selects nothing");
}

static void testSavedSelections()
{
    SceneDocument document;
    Entity a;
    a.id = "torch_0001";
    document.add(a);
    Entity b;
    b.id = "torch_0002";
    document.add(b);

    std::vector<selection::SelectionSet> sets;
    selection::save(sets, "torches", {"torch_0001", "torch_0002"});
    check(sets.size() == 1, "the set was saved");

    // Saving under the same name replaces rather than duplicating.
    selection::save(sets, "torches", {"torch_0001"});
    check(sets.size() == 1 && sets.front().members.size() == 1,
          "re-saving replaced the set");

    // An empty selection is not saved: a named set that restores nothing is a
    // trap, not a feature.
    selection::save(sets, "nothing", {});
    check(sets.size() == 1, "an empty selection was refused");

    selection::save(sets, "both", {"torch_0001", "torch_0002", "ghost_0001"});
    const std::vector<AuthorId> restored =
        selection::restore(sets, "both", document);
    check(restored.size() == 2,
          "an entity the document no longer has was dropped on restore");

    check(selection::restore(sets, "missing", document).empty(),
          "restoring a set that does not exist selects nothing");

    selection::remove(sets, "torches");
    check(sets.size() == 1 && sets.front().name == "both", "remove works");
}

int main()
{
    testRectNormalises();
    testMarqueeCatchesWhatItCrosses();
    testEncloseIsStricterThanTouch();
    testPickCycleWalksThenWraps();
    testPickCycleRestartsWhenTheClickMoves();
    testSavedSelections();

    if (gFailures != 0) {
        std::cerr << "EditorSelectionTests: " << gFailures << " failure(s)\n";
        return 1;
    }
    std::cout << "EditorSelectionTests: ok\n";
    return 0;
}
