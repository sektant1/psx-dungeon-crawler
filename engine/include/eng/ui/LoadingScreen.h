#pragma once

#include <eng/ui/UiCanvas.h>

#include <string>

namespace eng::ui {

// What the loading screen shows. Filled from a LoadRunner by the frame loop;
// the screen itself never touches the loader, so it can be driven by a fake
// progress source in a preview or a test.
struct LoadingView {
    float progress = 0.0f;   // 0..1
    std::string title;       // "ENTERING THE DUNGEON"
    std::string step;        // current step label
    std::string hint;        // optional flavour line at the bottom
    int completed = 0;       // steps finished
    int total = 0;           // steps in the plan
    float time = 0.0f;       // seconds since the screen appeared; drives animation
};

// Position of one rune in the spinner ring, in virtual pixels relative to the
// ring centre, plus its brightness in 0..1. Split out of the drawing code so
// the animation is verifiable without a window.
struct LoadingRune {
    int x = 0;
    int y = 0;
    float glow = 0.0f;
};

// The ring is a fixed number of runes; `index` walks them, `time` rotates the
// glow around the ring and `radius` is in virtual pixels.
LoadingRune loadingRune(int index, int count, float time, int radius);

// Torch flicker in 0..1: two out-of-phase sines, no randomness, so a
// deterministic capture of a loading frame is byte-identical every run.
float loadingFlicker(float time);

// Draws a full-screen loading screen onto an already-begun canvas. Retro
// dungeon theme: dark fill, rune ring spinner, chunky pixel progress bar.
void drawLoadingScreen(const UiCanvas& canvas, const LoadingView& view);

} // namespace eng::ui
