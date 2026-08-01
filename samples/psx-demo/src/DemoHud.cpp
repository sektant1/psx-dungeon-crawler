#include "DemoHud.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace {

std::string metres(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f m", double(value));
    return buffer;
}

std::string degrees(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.0f deg", double(value));
    return buffer;
}

std::string multiplier(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2fx", double(value));
    return buffer;
}

std::string counter(int index, int count)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d/%d", index + 1,
                  std::max(count, 1));
    return buffer;
}

} // namespace

bool DemoHud::initialise()
{
    eng::ui::TooltipStyle style;
    // Wider and taller than the crosshair tooltip: this one is a caption with a
    // control list, not a two-line label glanced at mid-fight.
    style.maxWidth = 208;
    style.minWidth = 176;
    style.maxBodyLines = 0; // the placard is a control list, not flavour text
    // Anchored into the corner, so the clearance the crosshair tooltip needs
    // (24 px, to stay off the thing it describes) would only push it inward.
    style.gap = 4;
    mTooltip.configure(style);
    return mCanvas.initialise();
}

void DemoHud::draw(const Status& status, float dt)
{
    if (!mCanvas.ready())
        return;

    eng::ui::TooltipContent content;
    if (mVisible) {
        // One stable id for the whole run: the panel is a caption, not a
        // look-target, so changing preset must not restart its fade.
        content.id = "demo/placard";
        content.title = "PSX ENGINE DEMO";
        content.meta = counter(status.presetIndex, status.presetCount);
        content.subtitle =
            status.paused ? status.preset + " - paused" : status.preset;
        content.accent =
            status.paused ? eng::ui::UiTone::Warning : eng::ui::UiTone::Focus;
        content.action = "CYCLE PRESET";
        content.actionKey = "TAB";
        // Dolly and lens, as gauges rather than as numbers in a list: the wheel
        // moves one and the panel moves with it. They are the two controls
        // whose effect on the image is easy to confuse, so both carry their
        // real value -- 6 m at 46 degrees and 12 m at 90 frame the dais
        // similarly and distort it completely differently.
        content.bars.push_back({"ZOOM", std::clamp(status.zoom, 0.0f, 1.0f),
                                eng::ui::UiTone::Focus,
                                metres(status.distance)});
        content.bars.push_back({"FOV", std::clamp(status.fov, 0.0f, 1.0f),
                                eng::ui::UiTone::Mystic,
                                degrees(status.fovDeg)});
        // Bindings as caps, not as prose. Packed into sentences the
        // punctuation keys were invisible: "` console ESC quit" read as a
        // typo, and ", . fov" as a comma splice.
        content.hints = {
            {"WHEEL", "dolly camera"},
            {"DRAG", "orbit view"},
            {", .", "field of view"},
            {"[ ]", "spin " + multiplier(status.orbitSpeed)},
            {"SPACE", status.paused ? "resume" : "pause"},
            {"R", "reset view"},
            {"F1", "tuning panel"},
            {"F4", "frame stats"},
            {"H", "hide this"},
            {"`", "console"},
            {"ESC", "quit"},
        };
    }
    mTooltip.update(content, dt);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    mCanvas.begin({display.x, display.y});

    // Top-left, anchored to the safe rect rather than to a fixed pixel: the
    // canvas magnification changes with the window, and a hard-coded point
    // would drift off the edge at the small end.
    const eng::ui::UiRect safe =
        eng::ui::UiRect{{0, 0}, mCanvas.size()}.inset({8, 8, 8, 8});
    eng::ui::TooltipAnchor anchor;
    anchor.mode = eng::ui::TooltipAnchor::Mode::Point;
    anchor.point = safe.position;
    mTooltip.draw(mCanvas, anchor, safe);
}
