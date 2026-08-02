#pragma once
#include "HudModel.h"
#include "ui/GameHud.h"

#include <eng/ui/Tooltip.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace ed {

// The 2D viewport: the game's real HUD, drawn inside an editor panel against a
// state the author dials in.
//
// It is the same `game::GameHud` the game runs, on the same `eng::ui::UiCanvas`,
// fed the same `HudSnapshot` -- not a mock-up. That is the whole point: a HUD
// preview that reimplements the HUD tells you about the preview. Everything
// here only decides *what state* to draw and *where*; the drawing is the
// shipped code, so a layout that reads here reads in the game.
//
// What it is not: a WYSIWYG layout editor. This HUD is authored in code plus a
// style sheet (`GameHudStyle.h`), not in a document, so there is nothing to
// drag. Dialling the state and seeing the result at every virtual resolution is
// the thing that was actually missing -- "does the vitals block still fit at
// 320x240 when the weapon name is long and three statuses are up" was a
// question you could only answer by playing until it happened.
struct UiStageState {
    // The virtual resolution the layout is drawn at, in virtual pixels. The
    // game picks this from the window; here the author picks it, because the
    // failures are at the extremes and the window is never at an extreme.
    glm::ivec2 virtualSize{320, 240};
    int scale = 2; // integer magnification, as the game's canvas does it

    // Safe area and keep-clear guides. A console HUD that ignores the safe area
    // is legible on the developer's monitor and cropped on a television.
    bool showSafeArea = true;
    int safeAreaPercent = 5;
    bool showGrid = false;
    bool showReticleCross = true;

    // The state the HUD is drawn against. Defaults are a healthy player with a
    // weapon, because that is the common case; the sliders are for the ones
    // that break the layout.
    float health = 1.0f;
    float stamina = 1.0f;
    float mana = 1.0f;
    float poise = 1.0f;
    std::string weaponName = "VESPER SPINDLE";
    std::string weaponDiscipline = "NEEDLE / PRECISION";
    int statusCount = 0;
    bool showTooltip = false;
    std::string tooltipTitle = "Iron Sconce";
    std::string tooltipBody = "cold to the touch";
    float dt = 1.0f / 60.0f;
};

// Builds the snapshot the stage draws. Pure, so the state -> HUD-input mapping
// is testable without a canvas: the interesting failures (a resource that reads
// full when it is empty, a status list longer than the array) are in here, not
// in the drawing.
game::HudSnapshot hudSnapshotFrom(const UiStageState& state);

// The tooltip content, likewise. Empty when the state does not ask for one.
eng::ui::TooltipContent hudTooltipFrom(const UiStageState& state);

// Largest integer magnification that fits `virtualSize` inside `available`
// pixels, clamped to [1,8]. Integer only: the canvas is a bitmap font on a
// pixel grid, and a fractional scale is how a retro HUD gets soft edges and
// uneven letter spacing.
int fitScale(glm::ivec2 virtualSize, glm::vec2 availablePixels);

} // namespace ed
