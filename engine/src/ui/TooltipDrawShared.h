#pragma once

#include <algorithm>
#include <cmath>

// Colour helpers shared by the tooltip and the target banner. Header-only and
// engine-private: they are two lines each and exist only so the two widgets
// cannot drift into slightly different fades.
namespace eng::ui::detail {

inline unsigned int fade(unsigned int colour, float alpha) {
    const unsigned int a = (colour >> 24) & 0xFFu;
    const unsigned int scaled =
        (unsigned int)std::lround(float(a) * std::clamp(alpha, 0.0f, 1.0f));
    return (colour & 0x00FFFFFFu) | (scaled << 24);
}

// Blend towards white by t, preserving alpha. The one-pixel highlight that
// keeps a dark panel from reading as a hole punched in the screen.
inline unsigned int lighten(unsigned int colour, float t) {
    const unsigned int a = colour & 0xFF000000u;
    auto mix = [t](unsigned int c) {
        return (unsigned int)std::lround(float(c) + (255.0f - float(c)) * t);
    };
    const unsigned int r = mix(colour & 0xFFu);
    const unsigned int g = mix((colour >> 8) & 0xFFu);
    const unsigned int b = mix((colour >> 16) & 0xFFu);
    return a | (b << 16) | (g << 8) | r;
}

} // namespace eng::ui::detail
