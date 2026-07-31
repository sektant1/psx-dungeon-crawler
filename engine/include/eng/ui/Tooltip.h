#pragma once

#include <eng/ui/UiCanvas.h>
#include <eng/ui/UiFade.h>
#include <eng/ui/UiLayout.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eng::ui {

// How prominently a look-target wants to be shown. This is *intent*, not
// layout: gameplay says "this is the thing being engaged with", and the HUD
// decides which surface that means. Keeping it here rather than in the HUD is
// what lets the same content be routed to either widget.
enum class TooltipEmphasis {
    Inline, // examined in passing -- the crosshair tooltip
    Focus,  // actively engaged with -- the target banner
};

// What a tooltip says. The engine never learns what a torch or a bandit is:
// gameplay fills this in, the presentation layer only lays it out.
struct TooltipContent {
    struct Line {
        std::string text;
        UiTone tone = UiTone::Muted;
    };
    struct Bar {
        std::string label;
        float ratio = 0.0f;
        UiTone tone = UiTone::Positive;
        std::string value; // optional readout drawn at the bar's right edge
    };

    std::string id;       // identity, so re-entering the same target does not restart the fade
    std::string title;
    std::string subtitle; // kind/category, drawn small under the title
    // Right-aligned against the title: one short measured value (distance,
    // tier, weight). It is metadata, not prose, and living in `lines` made it
    // indistinguishable from flavour text.
    std::string meta;
    UiTone accent = UiTone::Focus;
    TooltipEmphasis emphasis = TooltipEmphasis::Inline;
    std::vector<Line> lines;
    std::vector<Bar> bars;
    std::string action; // e.g. "OPEN", paired with a key cap
    std::string actionKey;

    bool empty() const {
        return title.empty() && subtitle.empty() && meta.empty() &&
               lines.empty() && bars.empty() && action.empty();
    }
};

// Anchors a tooltip near a point, or centres it under the crosshair.
struct TooltipAnchor {
    enum class Mode { Crosshair, Point };
    Mode mode = Mode::Crosshair;
    glm::ivec2 point{0, 0}; // virtual pixels, used when mode == Point
};

struct TooltipStyle {
    // Wide enough that a two-word name and a category each fit on one line.
    // The old 132 wrapped almost every real title, which turned a label into a
    // paragraph and flattened the hierarchy: everything became body text.
    int maxWidth = 184;
    int minWidth = 104;
    int padding = 6;
    // Clearance from the anchor. Large enough that the panel does not sit on
    // the thing it describes -- covering the enemy you are aiming at is worse
    // than showing nothing.
    int gap = 24;
    int safeMargin = 8;
    int maxBodyLines = 3;
    PanelPaint chrome{PanelStyle::Solid, RailEdge::Left, UiTone::Focus,
                      true, true, true};

    FadePace pace; // shared with the target banner; see UiFade.h
};

// Owns fade state and placement for one tooltip at a time. Kept separate from
// the HUD so any surface (world interaction, inventory, editor viewport) can
// present the same widget with the same behaviour.
//
// The pacing lives here and touches nothing but this object's own fields:
// `update` is a pure function of (content, dt), which is what lets the feel be
// pinned by tests instead of eyeballed frame by frame.
class TooltipView {
public:
    void configure(const TooltipStyle& style) {
        mStyle = style;
        mFade.configure(style.pace);
    }
    const TooltipStyle& style() const { return mStyle; }

    // Call every frame. Pass an empty content to dismiss.
    void update(const TooltipContent& content, float dt);

    // Lays the current tooltip out and draws it. Returns its rect, which is
    // useful for keeping other HUD elements clear of it.
    glm::ivec4 draw(const UiCanvas& canvas, const TooltipAnchor& anchor,
                    UiRect safeBounds = {}) const;

    float alpha() const { return mFade.alpha(); }
    bool visible() const { return mFade.visible() && !mContent.empty(); }
    const Fade& fade() const { return mFade; }

private:
    TooltipStyle mStyle;
    TooltipContent mContent;
    Fade mFade;
};

} // namespace eng::ui
