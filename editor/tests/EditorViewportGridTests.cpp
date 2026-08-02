// The work-plane grid's spacing, as a function of how far the camera is from
// it.
//
// The property that matters: the drawn grid always reaches past what the camera
// can see. A fixed patch of sixteen cells is a postage stamp under a framing
// shot two hundred metres up, which is exactly when an author needs a ground
// plane to read against.

#include <editor/viewport/ViewportGrid.h>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorViewportGridTests: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    const float kCell = 4.0f;
    const int kRadius = 16;

    // --- close in: the level's own cell, untouched --------------------------
    {
        const GridView view = gridViewFor(kCell, 2.0f, kRadius);
        require(view.cell == kCell && view.multiple == 1,
                "at head height the grid is the level's own cell");
        require(view.radius == kRadius, "and the requested radius");
    }

    // --- pulled back: coarser, and always past the horizon ------------------
    for (float height : {10.0f, 40.0f, 120.0f, 201.0f, 900.0f}) {
        const GridView view = gridViewFor(kCell, height, kRadius);
        const float extent = view.cell * float(view.radius);
        require(extent >= 3.0f * height,
                "the grid reaches past what the camera sees at height " +
                    std::to_string(height));
        require(view.cell >= kCell, "and never finer than the level's cell");
    }

    // --- every spacing stays a multiple the level could snap to -------------
    for (float height : {5.0f, 33.0f, 77.0f, 512.0f}) {
        const GridView view = gridViewFor(kCell, height, kRadius);
        require(view.multiple >= 1, "the multiple is positive");
        require((view.multiple & (view.multiple - 1)) == 0,
                "and a power of two");
        require(view.cell == kCell * float(view.multiple),
                "so a drawn line is always a line a piece could land on");
    }

    // --- monotonic: pulling back never makes the grid finer -----------------
    {
        int previous = 0;
        for (float height = 0.0f; height < 400.0f; height += 7.0f) {
            const GridView view = gridViewFor(kCell, height, kRadius);
            require(view.multiple >= previous,
                    "the spacing only ever grows with height");
            previous = view.multiple;
        }
    }

    // --- below the plane is the same distance as above ----------------------
    {
        require(gridViewFor(kCell, -80.0f, kRadius).multiple ==
                    gridViewFor(kCell, 80.0f, kRadius).multiple,
                "looking up at the plane from below coarsens it the same way");
    }

    // --- degenerate inputs do not divide by zero or hang --------------------
    {
        const GridView view = gridViewFor(0.0f, 1e9f, 0);
        require(view.cell > 0.0f && view.radius >= 1 && view.multiple <= 4096,
                "a zero cell, zero radius and absurd height stay bounded");
    }

    std::cout << "EditorViewportGridTests: ok\n";
    return 0;
}
