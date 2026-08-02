#pragma once
#include <editor/content/SceneDocument.h>

#include <glm/glm.hpp>

#include <string>

namespace ed {

// One piece per slot per stroke.
//
// The Place tool paints: it drops a piece every frame the button is held, so
// that dragging across a room fills it. What stops that from stacking a hundred
// copies in one spot is the slot -- a stroke remembers the slots it has already
// filled and skips them.
//
// A grid piece's slot is the cell and edge it snaps to, which is exact. A free
// prop has no cell, so its slot has to come from its position, quantized: a
// counter (what this used to be) is unique every frame, which makes the *frame
// rate* decide how many props a click drops.

std::string gridPaintSlot(const game::content::CellPlacement& cell);

// `spacing` is the grid step when snapping, and kFreePaintSpacing otherwise --
// the shortest distance apart two props may be painted in one stroke.
std::string freePaintSlot(const glm::vec3& position, float spacing);

// Half a metre: close enough to lay a row of candles by dragging, far enough
// that a click that wobbles by a pixel is still one candle.
constexpr float kFreePaintSpacing = 0.5f;

} // namespace ed
