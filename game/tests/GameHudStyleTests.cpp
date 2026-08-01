#include "ui/GameHudStyle.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "GameHudStyleTests: " << message << '\n';
        std::exit(1);
    }
}

unsigned int alpha(unsigned int colour) { return colour >> 24; }

} // namespace

int main()
{
    const game::GameHudStyleSheet normal =
        game::makeGameHudStyleSheet(0.75f, false, false);
    const game::GameHudStyleSheet again =
        game::makeGameHudStyleSheet(0.75f, false, false);
    require(normal.ui.palette.ink == again.ui.palette.ink &&
                normal.ui.palette.text == again.ui.palette.text,
            "style resolution must be idempotent");
    require(alpha(normal.ui.palette.ink) < 0xF0u,
            "opacity must affect panel fill");
    require(alpha(normal.ui.palette.text) == 0xFFu,
            "opacity must not dim critical text");
    const game::GameHudStyleSheet invalidOpacity =
        game::makeGameHudStyleSheet(
            std::numeric_limits<float>::quiet_NaN(), false, false);
    require(alpha(invalidOpacity.ui.palette.ink) == 0xF0u,
            "non-finite opacity must resolve to a safe default");

    const game::GameHudStyleSheet contrast =
        game::makeGameHudStyleSheet(1.0f, true, false);
    require(contrast.ui.palette.text == 0xFFFFFFFFu &&
                contrast.ui.palette.ink != normal.ui.palette.ink,
            "high contrast must resolve from a distinct base palette");

    const game::GameHudStyleSheet reduced =
        game::makeGameHudStyleSheet(1.0f, false, true);
    require(reduced.tooltip.pace.fadeIn == 0.0f &&
                reduced.tooltip.pace.fadeOut == 0.0f &&
                reduced.tooltip.pace.rise == 0,
            "reduced motion must remove travel and fades");

    const game::GameHudViewportStyle standard =
        game::resolveGameHudViewportStyle(normal, {960, 720});
    require(!standard.compact && standard.safe.size.x == 944,
            "standard canvas must retain corner-safe content width");

    const game::GameHudViewportStyle compact =
        game::resolveGameHudViewportStyle(normal, {320, 240});
    require(compact.compact && compact.vitalsWidth == 108 &&
                compact.safe.contains(compact.safe.position),
            "compact canvas must select narrow metrics and valid safe bounds");
    const game::GameHudBottomLayout compactBottom = game::layoutGameHudBottom(
        compact, {compact.vitalsWidth, 54}, {108, 22});
    require(compact.safe.contains(compactBottom.vitals) &&
                compact.safe.contains(compactBottom.armament),
            "compact bottom widgets must stay inside safe bounds");
    require(compactBottom.vitals.position.x + compactBottom.vitals.size.x <=
                compactBottom.armament.position.x,
            "compact bottom widgets must not overlap");

    const game::GameHudViewportStyle ultrawide =
        game::resolveGameHudViewportStyle(normal, {1280, 540});
    require(ultrawide.safe.size.x < 1280 && ultrawide.safe.position.x > 0,
            "ultrawide HUD must stay inside centered readable rails");

    const game::GameHudViewportStyle overscan =
        game::resolveGameHudViewportStyle(normal, {960, 720},
                                          {96, 72, 96, 72});
    require(overscan.safe.position.x == 104 && overscan.safe.position.y == 80 &&
                overscan.safe.size.x == 752 && overscan.safe.size.y == 560,
            "safe-area insets must affect real HUD layout, plus its own margin");
    const game::GameHudBottomLayout overscanBottom = game::layoutGameHudBottom(
        overscan, {overscan.vitalsWidth, 54}, {160, 22});
    require(overscan.safe.contains(overscanBottom.vitals) &&
                overscan.safe.contains(overscanBottom.armament),
            "bottom HUD must remain inside TV/phone safe bounds");

    std::cout << "GameHudStyleTests: OK\n";
    return 0;
}
