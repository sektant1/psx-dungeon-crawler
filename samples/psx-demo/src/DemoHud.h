#pragma once

#include <eng/ui/Tooltip.h>
#include <eng/ui/UiCanvas.h>

#include <string>

// The demo's placard, drawn with the engine's tooltip widget -- the same one
// the game puts under the crosshair for look-targets.
//
// It replaced a world-space text sprite hanging over the dais. That sprite was
// a scene object: it took the render profile's pixelation and dither, it swam
// with the turntable, it had to be destroyed and rebuilt to change one word,
// and at low preset resolutions its own label was unreadable. The tooltip is a
// virtual-pixel UI surface drawn after the post chain, so the demo's caption is
// crisp at every profile while the *world* keeps the look the demo is showing.
//
// Nothing here knows about presets or cameras: the app hands over a Status each
// frame and this decides how it reads.
class DemoHud {
public:
    struct Status {
        std::string preset;
        int presetIndex = 0;
        int presetCount = 1;
        float distance = 0.0f; // camera dolly, metres
        float zoom = 0.0f;     // 0..1 across the dolly range
        float fovDeg = 0.0f;
        float fov = 0.0f; // 0..1 across the lens range
        float orbitSpeed = 1.0f;
        bool paused = false;
    };

    // Loads the shared bitmap font. Returns false when the atlas is missing, in
    // which case draw() is a no-op rather than a crash.
    bool initialise();

    bool visible() const { return mVisible; }
    void toggle() { mVisible = !mVisible; }

    // Call inside the imgui frame. Hidden, it still ticks the fade out, so the
    // panel leaves the screen the way it arrived.
    void draw(const Status& status, float dt);

private:
    eng::ui::UiCanvas mCanvas;
    eng::ui::TooltipView mTooltip;
    bool mVisible = true;
};
