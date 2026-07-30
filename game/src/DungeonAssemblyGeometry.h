#pragma once

namespace game::assembly {

// Door_Frame_01 and Wall_01 have exactly coincident end/inner faces when they
// meet at a cell boundary. Pull only the flanking wall visual 1 cm into the
// frame so the hidden surfaces overlap instead of z-fighting. Collision stays
// on the authored cell boundary.
inline constexpr float kOpeningFlankOverlap = 0.01f;

constexpr float wallVisualInset(float baseInset, bool opening,
                                bool openingRunsNorthSouth, int dc, int dr)
{
    const bool flank = opening &&
        (openingRunsNorthSouth ? dc != 0 : dr != 0);
    return flank && baseInset > kOpeningFlankOverlap
               ? baseInset - kOpeningFlankOverlap
               : baseInset;
}

} // namespace game::assembly
