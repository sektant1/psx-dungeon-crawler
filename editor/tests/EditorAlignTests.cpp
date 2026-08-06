// Placement and alignment aids: Gregory §15.4.1.7, as arithmetic.
//
// Every one of these has the same failure mode -- "everything is now in one
// place" -- and by the time that is visible in a screenshot the author has
// already lost the layout. So they are checked here instead.

#include <editor/scene/AlignTools.h>

#include <cmath>
#include <iostream>
#include <string>

namespace align = ed::align;

static int gFailures = 0;

static void check(bool condition, const std::string& what)
{
    if (!condition) {
        std::cerr << "EditorAlignTests: " << what << '\n';
        ++gFailures;
    }
}

static bool near(float a, float b)
{
    return std::abs(a - b) < 1e-4f;
}

// A box whose origin sits at its centre, `half` in each direction.
static align::Placement box(const std::string& id, glm::vec3 at, float half)
{
    align::Placement p;
    p.id = id;
    p.position = at;
    p.boundsMin = at - glm::vec3(half);
    p.boundsMax = at + glm::vec3(half);
    return p;
}

// A prop whose origin is at its BASE, not its centre -- the case that catches
// an implementation which sets positions to the target instead of moving by a
// delta.
static align::Placement standing(const std::string& id, glm::vec3 base,
                                 float half, float height)
{
    align::Placement p;
    p.id = id;
    p.position = base;
    p.boundsMin = {base.x - half, base.y, base.z - half};
    p.boundsMax = {base.x + half, base.y + height, base.z + half};
    return p;
}

static void testAlignMinUsesBounds()
{
    const std::vector<align::Placement> boxes = {
        box("a", {0.0f, 0.0f, 0.0f}, 1.0f),  // min x = -1
        box("b", {10.0f, 0.0f, 0.0f}, 2.0f), // min x = 8
    };
    const std::vector<align::Move> moves =
        align::alignTo(boxes, align::Axis::X, align::Mode::Min);
    check(moves.size() == 2, "both entities came back");
    // The lowest min in the selection is -1, so the wide box moves left until
    // its own min reaches -1: origin 10 - (8 - -1) = 1.
    check(near(moves[0].position.x, 0.0f), "the leftmost box did not move");
    check(near(moves[1].position.x, 1.0f),
          "the wide box lined its own edge up, not its origin");
}

static void testAlignCentreIsIdempotent()
{
    const std::vector<align::Placement> boxes = {
        box("a", {0.0f, 0.0f, 0.0f}, 1.0f),
        box("b", {10.0f, 0.0f, 0.0f}, 1.0f),
        box("c", {4.0f, 0.0f, 0.0f}, 1.0f),
    };
    std::vector<align::Move> moves =
        align::alignTo(boxes, align::Axis::X, align::Mode::Centre);
    check(moves.size() == 3, "all three came back");
    for (const align::Move& move : moves)
        check(near(move.position.x, 5.0f),
              "everything landed on the midpoint of the spread");

    // Running it again must change nothing -- the target is the spread, not
    // the primary, so the operation has a fixed point.
    std::vector<align::Placement> after;
    for (const align::Move& move : moves)
        after.push_back(box(move.id, move.position, 1.0f));
    for (const align::Move& move :
         align::alignTo(after, align::Axis::X, align::Mode::Centre))
        check(near(move.position.x, 5.0f), "a second pass moved nothing");
}

static void testAlignRespectsOffsetPivots()
{
    // Two standing props whose origins are at their bases. Aligning their tops
    // (max Y) must not collapse them onto one another's origins.
    const std::vector<align::Placement> props = {
        standing("short", {0.0f, 0.0f, 0.0f}, 0.5f, 2.0f), // top at 2
        standing("tall", {5.0f, 0.0f, 0.0f}, 0.5f, 3.0f),  // top at 3
    };
    const std::vector<align::Move> moves =
        align::alignTo(props, align::Axis::Y, align::Mode::Max);
    check(near(moves[0].position.y, 1.0f),
          "the short prop rose so its top met the tallest");
    check(near(moves[1].position.y, 0.0f), "the tall prop stayed put");
}

static void testAlignNeedsTwo()
{
    const std::vector<align::Placement> one = {box("a", {3.0f, 0, 0}, 1.0f)};
    check(align::alignTo(one, align::Axis::X, align::Mode::Min).empty(),
          "one entity is already aligned with itself");
    check(align::alignTo({}, align::Axis::X, align::Mode::Min).empty(),
          "an empty selection is a no-op");
}

static void testDistributeKeepsTheEnds()
{
    // Deliberately out of order, and bunched: the middle two should spread.
    const std::vector<align::Placement> boxes = {
        box("d", {9.0f, 0.0f, 0.0f}, 0.5f),
        box("a", {0.0f, 0.0f, 0.0f}, 0.5f),
        box("c", {2.0f, 0.0f, 0.0f}, 0.5f),
        box("b", {1.0f, 0.0f, 0.0f}, 0.5f),
    };
    const std::vector<align::Move> moves =
        align::distribute(boxes, align::Axis::X);
    check(moves.size() == 4, "everything came back");

    // Sorted by centre inside, so the result is independent of selection
    // order: 0, 3, 6, 9.
    check(moves[0].id == "a" && near(moves[0].position.x, 0.0f),
          "the first end stayed put");
    check(moves[1].id == "b" && near(moves[1].position.x, 3.0f),
          "the second landed a third of the way");
    check(moves[2].id == "c" && near(moves[2].position.x, 6.0f),
          "the third landed two thirds of the way");
    check(moves[3].id == "d" && near(moves[3].position.x, 9.0f),
          "the last end stayed put");
}

static void testDistributeNeedsThree()
{
    const std::vector<align::Placement> two = {
        box("a", {0.0f, 0, 0}, 0.5f),
        box("b", {5.0f, 0, 0}, 0.5f),
    };
    check(align::distribute(two, align::Axis::X).empty(),
          "nothing lies between two things");
}

static void testDropRestsUndersideOnFloor()
{
    const std::vector<align::Placement> props = {
        // A crate floating three metres up, origin at its centre.
        box("crate", {0.0f, 3.0f, 0.0f}, 0.5f), // underside at 2.5
        standing("post", {4.0f, 6.0f, 0.0f}, 0.25f, 2.0f), // underside at 6
        box("orphan", {8.0f, 5.0f, 0.0f}, 0.5f),
    };
    const std::vector<align::Drop> floors = {
        {"crate", 0.0f},
        {"post", 1.5f},
        // "orphan" deliberately absent: nothing was under it.
    };
    const std::vector<align::Move> moves = align::dropTo(props, floors);
    check(moves.size() == 2, "only the two with a floor moved");
    check(near(moves[0].position.y, 0.5f),
          "the centre-pivot crate rests its underside on the floor");
    check(near(moves[1].position.y, 1.5f),
          "the base-pivot post rests on its floor");
    for (const align::Move& move : moves)
        check(move.id != "orphan", "an entity with no floor was left alone");
}

int main()
{
    testAlignMinUsesBounds();
    testAlignCentreIsIdempotent();
    testAlignRespectsOffsetPivots();
    testAlignNeedsTwo();
    testDistributeKeepsTheEnds();
    testDistributeNeedsThree();
    testDropRestsUndersideOnFloor();

    if (gFailures != 0) {
        std::cerr << "EditorAlignTests: " << gFailures << " failure(s)\n";
        return 1;
    }
    std::cout << "EditorAlignTests: ok\n";
    return 0;
}
