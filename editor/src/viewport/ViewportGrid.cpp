#include <editor/viewport/ViewportGrid.h>

#include <cmath>

namespace ed {

GridView gridViewFor(float baseCell, float heightAbovePlane, int radius)
{
    GridView view;
    view.radius = radius > 0 ? radius : 1;
    view.cell = baseCell > 0.0f ? baseCell : 1.0f;
    view.multiple = 1;

    // A perspective camera at height h looking down sees roughly 2h of ground
    // across the viewport; three times that gives the grid enough margin to
    // still reach the edges when the view is tilted rather than straight down.
    const float height = std::fabs(heightAbovePlane);
    const float wantedExtent = 3.0f * height;
    const float wantedCell = wantedExtent / float(view.radius);

    // Powers of two only: every line drawn stays a line the level could snap
    // to, and a coarse grid is still an honest multiple of the fine one.
    while (view.cell < wantedCell && view.multiple < 4096) {
        view.cell *= 2.0f;
        view.multiple *= 2;
    }
    return view;
}

} // namespace ed
