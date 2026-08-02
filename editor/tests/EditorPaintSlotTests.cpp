// Paint slots: what stops one click from dropping a pile.
//
// The Place tool places every frame the button is held. Dedup by slot is the
// only thing between that and a hundred props in one spot, and the free-prop
// slot used to be a counter -- unique every frame, so the frame rate decided
// how many props a click produced.

#include <editor/scene/PaintSlot.h>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;
using game::content::CellPlacement;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorPaintSlotTests: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    // --- grid pieces --------------------------------------------------------
    CellPlacement cell;
    cell.col = 3;
    cell.row = 4;
    require(gridPaintSlot(cell) == gridPaintSlot(cell),
            "the same cell is the same slot");

    CellPlacement other = cell;
    other.col = 4;
    require(gridPaintSlot(cell) != gridPaintSlot(other),
            "a neighbouring cell is a different slot");

    other = cell;
    other.edge = CellPlacement::Edge::North;
    require(gridPaintSlot(cell) != gridPaintSlot(other),
            "an edge piece does not collide with the floor of its cell");

    other = cell;
    other.level = 4.0f;
    require(gridPaintSlot(cell) != gridPaintSlot(other),
            "two work planes are two slots");

    // --- free props ---------------------------------------------------------
    const glm::vec3 origin{2.0f, 0.0f, -3.0f};
    require(freePaintSlot(origin, 0.5f) == freePaintSlot(origin, 0.5f),
            "a stationary cursor is one slot, however many frames it is held");

    // A click that wobbles by a few millimetres is still one prop.
    require(freePaintSlot(origin, 0.5f) ==
                freePaintSlot(origin + glm::vec3(0.01f, 0.0f, -0.02f), 0.5f),
            "sub-spacing jitter does not open a new slot");

    // Dragging further than the spacing paints the next one.
    require(freePaintSlot(origin, 0.5f) !=
                freePaintSlot(origin + glm::vec3(0.6f, 0.0f, 0.0f), 0.5f),
            "moving a full step apart is a new slot");
    require(freePaintSlot(origin, 4.0f) ==
                freePaintSlot(origin + glm::vec3(0.6f, 0.0f, 0.0f), 4.0f),
            "and the spacing is what decides how far that is");

    // Height counts: a candle on a shelf is not the candle below it.
    require(freePaintSlot(origin, 0.5f) !=
                freePaintSlot(origin + glm::vec3(0.0f, 2.0f, 0.0f), 0.5f),
            "a prop above another is a different slot");

    // A degenerate spacing must not divide by zero into one slot for the world.
    require(freePaintSlot(origin, 0.0f) !=
                freePaintSlot(origin + glm::vec3(4.0f, 0.0f, 0.0f), 0.0f),
            "a zero spacing falls back to the default rather than collapsing");

    std::cout << "EditorPaintSlotTests: ok\n";
    return 0;
}
