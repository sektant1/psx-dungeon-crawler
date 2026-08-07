#pragma once

#include "Image.h"

#include <eng/Sprite.h>

#include <string>

namespace eng::rhi_renderer {

// Draws a world-space text label into RGBA pixels, ready to be uploaded as the
// texture of a billboard sprite.
//
// A separate rasteriser rather than a reuse of eng::ui::BitmapFont because the
// two want opposite things from the same atlas: BitmapFont emits ImGui quads
// that sample an *uploaded* atlas at draw time, while a world label is baked
// once into a texture of its own and then never touched again. Sharing the
// class would mean either giving it a CPU copy of the atlas it does not need or
// giving the renderer an ImDrawList it has no business holding.
//
// Style semantics, since they are not all obvious from the field names:
//   - the plate is filled with backgroundColour, outlined one pixel in
//     borderColour, and carries an accent bar accentWidthPixels wide down its
//     left edge in accentColour;
//   - text is drawn in textColour, except on a line containing the `pattern` of
//     a colour rule, which takes that rule's colour. First matching rule wins,
//     so the more specific rules belong first;
//   - text is word-wrapped to maxWidthPixels, which measures the text alone --
//     padding, border and accent are added around the result.
//
// Returns false (leaving `out` untouched) when the font cannot be loaded or the
// text is empty.
bool rasterizeLabel(const std::string& text, const TextSpriteStyle& style,
                    Image& out);

} // namespace eng::rhi_renderer
