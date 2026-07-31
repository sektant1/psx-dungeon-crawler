#include "GameHudStyle.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace game {
namespace {

unsigned int withAlpha(unsigned int colour, float alpha)
{
    if (!std::isfinite(alpha))
        alpha = 1.0f;
    const unsigned int source = (colour >> 24) & 0xFFu;
    const unsigned int resolved = static_cast<unsigned int>(std::lround(
        float(source) * std::clamp(alpha, 0.0f, 1.0f)));
    return (colour & 0x00FFFFFFu) | (resolved << 24);
}

} // namespace

GameHudStyleSheet makeGameHudStyleSheet(float opacity, bool highContrast,
                                        bool reducedMotion)
{
    GameHudStyleSheet style;
    eng::ui::UiPalette& palette = style.ui.palette;

    // Ash Reliquary: black iron, bone ink, brass focus, blood danger and
    // restrained ritual violet. Packed in ImGui ABGR order.
    palette.ink = 0xF00A0E12U;
    palette.inkSoft = 0xF012171DU;
    palette.edge = 0xFF33546BU;
    palette.edgeBright = 0xFF5593B8U;
    palette.text = 0xFFC0DCE8U;
    palette.textDim = 0xFF68818EU;
    palette.shadow = 0xC0000000U;
    palette.accent = 0xFF5CB9F0U;
    palette.good = 0xFF6FA56FU;
    palette.warn = 0xFF41A4D9U;
    palette.bad = 0xFF464AC7U;
    palette.mystic = 0xFFC9838FU;

    if (highContrast) {
        palette.ink = 0xFA000000U;
        palette.inkSoft = 0xFA101010U;
        palette.edge = 0xFFE0C080U;
        palette.edgeBright = 0xFFFFE0A0U;
        palette.text = 0xFFFFFFFFU;
        palette.textDim = 0xFFD8D8D8U;
        palette.accent = 0xFF70D8FFU;
        palette.good = 0xFF80E080U;
        palette.warn = 0xFF50C8FFU;
        palette.bad = 0xFF7070FFU;
        palette.mystic = 0xFFFF90E0U;
    }
    palette.ink = withAlpha(palette.ink, opacity);
    palette.inkSoft = withAlpha(palette.inkSoft, opacity);
    palette.shadow = withAlpha(palette.shadow, opacity);

    style.vitals = {eng::ui::PanelStyle::Solid, eng::ui::RailEdge::Left,
                    eng::ui::UiTone::Danger, true, true, true};
    style.armament = {eng::ui::PanelStyle::Solid, eng::ui::RailEdge::Right,
                      eng::ui::UiTone::Focus, true, true, true};
    style.status = {eng::ui::PanelStyle::Sunken, eng::ui::RailEdge::Left,
                    eng::ui::UiTone::Warning, false, true, false};
    style.ui.tooltip = {eng::ui::PanelStyle::Solid, eng::ui::RailEdge::Left,
                        eng::ui::UiTone::Focus, true, true, true};
    style.ui.banner = {eng::ui::PanelStyle::Solid, eng::ui::RailEdge::Bottom,
                       eng::ui::UiTone::Danger, true, true, true};

    style.tooltip.maxWidth = style.standardTooltipWidth;
    style.tooltip.minWidth = 104;
    style.tooltip.padding = style.ui.spacing.padding;
    style.tooltip.gap = 20;
    style.tooltip.safeMargin = style.ui.spacing.safe;
    style.tooltip.maxBodyLines = 3;
    style.tooltip.chrome = style.ui.tooltip;

    style.banner.widthFraction = 0.44f;
    style.banner.minWidth = 150;
    style.banner.maxWidth = 320;
    style.banner.padding = style.ui.spacing.padding;
    style.banner.topMargin = style.ui.spacing.safe;
    style.banner.barHeight = 6;
    style.banner.chrome = style.ui.banner;

    if (reducedMotion) {
        style.tooltip.pace.fadeIn = 0.0f;
        style.tooltip.pace.fadeOut = 0.0f;
        style.tooltip.pace.rise = 0;
        style.tooltip.pace.swapPunch = 0.0f;
        style.banner.pace = style.tooltip.pace;
    }
    return style;
}

GameHudViewportStyle resolveGameHudViewportStyle(
    const GameHudStyleSheet& style, glm::ivec2 canvasSize)
{
    canvasSize = glm::max(canvasSize, glm::ivec2(1));
    GameHudViewportStyle resolved;
    resolved.compact = canvasSize.x < style.compactWidth ||
                       canvasSize.y < style.compactHeight;
    resolved.margin = resolved.compact ? 6 : style.ui.spacing.safe;
    resolved.gap = style.ui.spacing.gap;
    resolved.vitalsWidth = resolved.compact ? style.compactVitalsWidth
                                             : style.standardVitalsWidth;
    resolved.tooltipWidth = resolved.compact ? style.compactTooltipWidth
                                              : style.standardTooltipWidth;

    int contentWidth = canvasSize.x;
    const float aspect = float(canvasSize.x) / float(canvasSize.y);
    if (aspect > style.ultrawideAspect)
        contentWidth = std::min(contentWidth,
                                int(std::lround(float(canvasSize.y) *
                                                style.contentAspect)));
    const int contentX = (canvasSize.x - contentWidth) / 2;
    resolved.safe = eng::ui::UiRect{{contentX, 0}, {contentWidth, canvasSize.y}}
                        .inset({resolved.margin, resolved.margin,
                                resolved.margin, resolved.margin});
    resolved.vitalsWidth = std::min(resolved.vitalsWidth,
                                    std::max(1, resolved.safe.size.x / 2 -
                                                    resolved.gap));
    resolved.tooltipWidth = std::min(resolved.tooltipWidth,
                                     std::max(1, resolved.safe.size.x));
    return resolved;
}

GameHudBottomLayout layoutGameHudBottom(const GameHudViewportStyle& viewport,
                                        glm::ivec2 vitalsSize,
                                        glm::ivec2 armamentSize)
{
    vitalsSize = glm::max(vitalsSize, glm::ivec2(0));
    armamentSize = glm::max(armamentSize, glm::ivec2(0));
    const int height = std::max(vitalsSize.y, armamentSize.y);
    const int bottom = viewport.safe.position.y + viewport.safe.size.y;
    const eng::ui::UiRect bounds{
        {viewport.safe.position.x, bottom - height},
        {viewport.safe.size.x, height}};
    const std::array items{
        eng::ui::FlexItem{vitalsSize,
                          {std::min(vitalsSize.x, 72), vitalsSize.y}, 0, 1},
        eng::ui::FlexItem{armamentSize,
                          {std::min(armamentSize.x, 72), armamentSize.y}, 0, 1},
    };
    std::array<eng::ui::UiRect, 2> output{};
    eng::ui::FlexLayout layout;
    layout.justify = eng::ui::FlexJustify::SpaceBetween;
    layout.align = eng::ui::FlexAlign::End;
    layout.gap = viewport.gap;
    (void)eng::ui::layoutFlex(bounds, layout, items, output);
    return {output[0], output[1]};
}

} // namespace game
