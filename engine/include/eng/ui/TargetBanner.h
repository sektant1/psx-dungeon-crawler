#pragma once

#include <eng/ui/Tooltip.h>
#include <eng/ui/UiCanvas.h>
#include <eng/ui/UiFade.h>

#include <glm/glm.hpp>

namespace eng::ui {

// A wide banner pinned to the top-centre of the screen, for the one thing the
// player is currently engaging with.
//
// This is a *presentation* of the same TooltipContent the crosshair tooltip
// takes, not a second content model. A creature and a barrel are described by
// gameplay in exactly the same terms; only where and how large they are drawn
// differs, and that difference is this class.
//
// Why a banner rather than a tooltip for enemies: a crosshair-anchored panel
// sits on top of the thing you are aiming at, which for a target you are
// actively fighting is the worst possible place for it. A top-centre bar keeps
// the health readable while the enemy stays unobstructed.
//
// Pacing is the shared eng::ui::Fade, so the banner and the tooltip cannot
// drift into different feels.
struct TargetBannerStyle {
    // A fraction of the virtual screen width, clamped to the bounds below, so
    // the banner scales with the canvas instead of being a fixed pixel slab.
    float widthFraction = 0.42f;
    int minWidth = 150;
    int maxWidth = 320;
    int padding = 5;
    int topMargin = 12;
    int barHeight = 7;
    PanelPaint chrome{PanelStyle::Solid, RailEdge::Bottom, UiTone::Danger,
                      true, true, true};
    FadePace pace;
};

class TargetBanner {
public:
    void configure(const TargetBannerStyle& style) {
        mStyle = style;
        mFade.configure(style.pace);
    }
    const TargetBannerStyle& style() const { return mStyle; }

    // Call every frame; empty content dismisses it.
    void update(const TooltipContent& content, float dt);

    // Draws it and returns its rect, so other HUD elements can stay clear.
    glm::ivec4 draw(const UiCanvas& canvas, UiRect safeBounds = {}) const;

    bool visible() const { return mFade.visible() && !mContent.empty(); }
    const Fade& fade() const { return mFade; }

private:
    TargetBannerStyle mStyle;
    TooltipContent mContent;
    Fade mFade;
};

} // namespace eng::ui
