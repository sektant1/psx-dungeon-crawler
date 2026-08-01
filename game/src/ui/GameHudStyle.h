#pragma once

#include <eng/ui/TargetBanner.h>
#include <eng/ui/Tooltip.h>
#include <eng/ui/UiCanvas.h>
#include <eng/ui/UiLayout.h>

namespace game {

// Game-owned stylesheet. Engine UI provides mechanisms and semantic paint
// roles; dark-fantasy visual policy stays here.
struct GameHudStyleSheet {
    eng::ui::UiStyleSheet ui;
    eng::ui::TooltipStyle tooltip;
    eng::ui::TargetBannerStyle banner;

    eng::ui::PanelPaint vitals;
    eng::ui::PanelPaint armament;
    eng::ui::PanelPaint status;

    int compactWidth = 480;
    int compactHeight = 320;
    int standardVitalsWidth = 150;
    int compactVitalsWidth = 108;
    int standardTooltipWidth = 176;
    int compactTooltipWidth = 160;
    float ultrawideAspect = 2.0f;
    float contentAspect = 16.0f / 9.0f;
};

struct GameHudViewportStyle {
    eng::ui::UiRect safe;
    bool compact = false;
    int margin = 8;
    int gap = 4;
    int vitalsWidth = 150;
    int tooltipWidth = 176;
};

struct GameHudBottomLayout {
    eng::ui::UiRect vitals;
    eng::ui::UiRect armament;
};

GameHudStyleSheet makeGameHudStyleSheet(float opacity, bool highContrast,
                                         bool reducedMotion);
GameHudViewportStyle resolveGameHudViewportStyle(
    const GameHudStyleSheet& style, glm::ivec2 canvasSize,
    eng::ui::Insets safeArea = {});
GameHudBottomLayout layoutGameHudBottom(const GameHudViewportStyle& viewport,
                                        glm::ivec2 vitalsSize,
                                        glm::ivec2 armamentSize);

} // namespace game
