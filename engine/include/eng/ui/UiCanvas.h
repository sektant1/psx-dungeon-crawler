#pragma once

#include <eng/ui/BitmapFont.h>

#include <glm/glm.hpp>

#include <string>
#include <string_view>

struct ImDrawList;

namespace eng::ui {

// Palette entries are named by role, not by colour, so a theme swap does not
// touch a single widget.
struct UiPalette {
    // Packed as imgui does it: 0xAABBGGRR, i.e. the byte order is R,G,B,A in
    // memory. Writing these as if they were 0xAARRGGBB is the classic way to
    // get a gold UI that renders blue.
    unsigned int ink = 0xF00A0E12U;        // panel fill
    unsigned int inkSoft = 0xF012171DU;    // secondary fill
    unsigned int edge = 0xFF33546BU;       // panel border
    unsigned int edgeBright = 0xFF5593B8U;
    unsigned int text = 0xFFC0DCE8U;
    unsigned int textDim = 0xFF68818EU;
    unsigned int shadow = 0xC0000000U;
    unsigned int accent = 0xFF5CB9F0U;     // candle gold
    unsigned int good = 0xFF6FA56FU;       // moss green
    unsigned int warn = 0xFF41A4D9U;       // ember amber
    unsigned int bad = 0xFF464AC7U;        // blood red
    unsigned int mystic = 0xFFC9838FU;     // ritual violet
};

// Semantic colours are the UI equivalent of CSS custom properties. Content
// asks for meaning; the active palette decides the actual packed colour.
enum class UiTone {
    Text,
    Muted,
    Focus,
    Positive,
    Warning,
    Danger,
    Mystic,
    Edge,
};

enum class PanelStyle {
    Solid,  // filled, single-pixel border
    Frame,  // border only
    Sunken, // filled with an inner shade, for slots and bars
};

enum class Align { Left, Centre, Right };

enum class RailEdge { None, Left, Right, Bottom };

struct PanelPaint {
    PanelStyle style = PanelStyle::Solid;
    RailEdge rail = RailEdge::None;
    UiTone railTone = UiTone::Focus;
    bool highlight = true;
    bool brokenFrame = true;
    bool pins = true;
};

struct UiSpacing {
    int hairline = 1;
    int tight = 2;
    int gap = 4;
    int padding = 6;
    int safe = 8;
    int section = 10;
    int major = 16;
};

// Typed stylesheet: intentionally no selectors, cascade, DOM or retained
// widget tree. Immediate-mode widgets consume resolved paint and spacing values
// exactly like CSS consumers use variables and component classes.
struct UiStyleSheet {
    UiPalette palette;
    UiSpacing spacing;
    PanelPaint panel;
    PanelPaint inset{PanelStyle::Sunken, RailEdge::None, UiTone::Edge,
                     false, true, false};
    PanelPaint tooltip{PanelStyle::Solid, RailEdge::Left, UiTone::Focus,
                       true, true, true};
    PanelPaint banner{PanelStyle::Solid, RailEdge::Bottom, UiTone::Danger,
                      true, true, true};
};

// A screen-space drawing surface addressed in *virtual* pixels.
//
// Every coordinate the game passes is a virtual pixel; the canvas magnifies by
// an integer factor and snaps to whole device pixels. That is what makes the
// retro UI pixel-exact at any window size, and it is why nothing here takes a
// float "scale" the way the old HUD did -- half-pixel geometry is unreachable
// by construction.
//
// It draws into ImGui's foreground draw list, so the UI paints after the PSX
// post chain and stays crisp; the pixel look comes from the grid and the
// bitmap font, not from the compositor. The world image is untouched.
class UiCanvas {
public:
    // Loads the shared font. Safe to call once at startup.
    bool initialise(const std::string& fontDefinition = "ui_regular.toml");
    bool ready() const { return mFont.valid(); }

    // `preferred` is the smallest virtual resolution the layout is authored
    // against; the canvas picks the largest integer magnification that still
    // fits it, then reports the true virtual size (usually wider).
    void begin(glm::vec2 displayPixels, glm::ivec2 preferred = {640, 480});

    glm::ivec2 size() const { return mVirtual; }
    int scale() const { return mScale; }
    const BitmapFont& font() const { return mFont; }
    UiPalette& palette() { return mStyle.palette; }
    const UiPalette& palette() const { return mStyle.palette; }
    UiStyleSheet& style() { return mStyle; }
    const UiStyleSheet& style() const { return mStyle; }
    unsigned int colour(UiTone tone) const;

    // --- primitives, all in virtual pixels -------------------------------
    void rect(glm::ivec2 at, glm::ivec2 size, unsigned int colour) const;
    void border(glm::ivec2 at, glm::ivec2 size, unsigned int colour) const;
    void panel(glm::ivec2 at, glm::ivec2 size, PanelStyle style) const;
    void panel(glm::ivec2 at, glm::ivec2 size, const PanelPaint& paint,
               UiTone railTone, float opacity = 1.0f) const;
    void text(glm::ivec2 at, std::string_view value, unsigned int colour,
              Align align = Align::Left, bool shadow = true) const;
    void bar(glm::ivec2 at, glm::ivec2 size, float ratio, unsigned int fill,
             unsigned int track) const;
    void icon(glm::ivec2 at, glm::ivec2 size, unsigned int colour,
              int inset = 0) const;

    glm::ivec2 measure(std::string_view value) const {
        return mFont.measure(value);
    }
    int lineHeight() const { return mFont.lineHeight(); }

private:
    glm::vec2 toScreen(glm::ivec2 at) const;
    ImDrawList* list() const;

    BitmapFont mFont;
    UiStyleSheet mStyle;
    glm::ivec2 mVirtual{320, 240};
    glm::vec2 mDisplay{0.0f};
    int mScale = 1;
};

} // namespace eng::ui
