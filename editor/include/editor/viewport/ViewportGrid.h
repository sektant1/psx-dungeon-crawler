#pragma once

namespace ed {

// How coarse the work-plane grid should be drawn, given how far the camera is
// from it.
//
// The grid used to be a fixed patch: sixteen cells either side of the camera at
// the level's own cell size. Framed on a whole dungeon from a couple of hundred
// metres up, that patch is a postage stamp floating under the view while the
// level sits somewhere else on screen -- it stops reading as "the ground" at
// exactly the height where the author most needs a ground plane.
//
// So the spacing steps up with height, always as a power-of-two multiple of the
// cell, which keeps every drawn line a line the level itself could snap to and
// keeps the line count constant.
struct GridView {
    float cell = 4.0f; // metres between lines
    int radius = 16;   // lines either side of the centre
    // How many of the level's own cells one drawn cell covers: 1, 2, 4, ...
    // Shown in the toolbar so a coarse grid never lies about the scale.
    int multiple = 1;
};

GridView gridViewFor(float baseCell, float heightAbovePlane, int radius);

} // namespace ed
