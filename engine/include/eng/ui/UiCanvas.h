#pragma once

#include <eng/ui/BitmapFont.h>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

struct ImDrawList;

namespace eng::ui {

// Palette entries are named by role, not by colour, so a theme swap does not
// touch a single widget.
struct UiPalette {
    // Packed as imgui does it: 0xAABBGGRR, i.e. the byte order is R,G,B,A in
    // memory. Writing these as if they were 0xAARRGGBB is the classic way to
    // get a gold UI that renders blue.
    // AC Vixen, the engine logo's palette: a gunmetal shell carrying colour
    // only where colour means something. The chassis is deliberately neutral
    // (R=G=B) so the readouts are the only saturated thing on screen -- that
    // restraint is what makes a bar draw the eye without any of it flashing.
    unsigned int ink = 0xF01A1616U;        // panel fill, near-black chrome
    unsigned int inkSoft = 0xF0201C1CU;    // secondary fill
    unsigned int edge = 0xFF3F3A3AU;       // panel border, gunmetal
    unsigned int edgeBright = 0xFF756E6EU; // lit edge
    unsigned int text = 0xFFCCC8C8U;       // brushed steel
    unsigned int textDim = 0xFF6E6A6AU;
    unsigned int shadow = 0xC0000000U;
    // The one hot colour, and the only place it is spent: whatever currently
    // has focus. Same magenta as the mech's shoulder strips.
    unsigned int accent = 0xFFEFACF0U;
    // The three readouts. Muted rather than primary -- they have to be
    // distinguishable at a glance without competing with the accent.
    unsigned int good = 0xFF8CA56FU;       // STA, desaturated teal-green
    unsigned int warn = 0xFF508AC0U;       // low, muted amber
    unsigned int bad = 0xFF5A50B4U;        // VIT, muted crimson
    unsigned int mystic = 0xFFC078C0U;     // ARC, violet from the accent family
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
    //
    // Draws over the whole window, on imgui's foreground list: that is what a
    // game HUD is. Use beginTarget() to put the same canvas inside a panel.
    void begin(glm::vec2 displayPixels, glm::ivec2 preferred = {640, 480},
               glm::vec2 framebufferScale = {1.0f, 1.0f});

    // The same canvas, drawn into a rectangle of somebody else's window.
    //
    // Exists because a canvas that can only paint the whole window at the
    // window's own origin can never be previewed, embedded or composited -- and
    // the editor's job is to show the HUD *beside* the thing it is being
    // authored against, at a virtual resolution the author picks rather than
    // the one the window happens to give.
    //
    //   originPixels  screen position of virtual (0,0)
    //   scale         integer magnification, >= 1
    //   virtualSize   how many virtual pixels the surface is; layouts anchor to
    //                 its corners exactly as they anchor to the window's
    //   target        the draw list to paint onto (a window's, so it clips and
    //                 z-orders with the panel). Null means the foreground list.
    void beginTarget(glm::vec2 originPixels, glm::ivec2 virtualSize, int scale,
                     ImDrawList* target);

    glm::ivec2 size() const { return mVirtual; }
    int scale() const { return mScale; }
    const BitmapFont& font() const { return mFont; }

    // A named face, loaded on first use and kept.
    //
    // One canvas, several fonts: a heading and a body line want different
    // faces, and a canvas that could only hold the one it was initialised with
    // forced every screen in the game to speak in a single voice. An empty
    // name, or one that will not load, is the canvas's own font -- a missing
    // atlas must degrade to readable text, never to no text.
    const BitmapFont& fontFor(const std::string& definition) const;
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
    // The same, in a named face at an integer magnification.
    //
    // `textScale` multiplies the canvas's own, so a 2 is twice the size at
    // every window size rather than twice the size at one of them. Integer for
    // the reason the canvas scale is: a bitmap font at 1.5x has uneven letter
    // spacing and soft edges, which is the whole thing this UI is avoiding.
    void text(glm::ivec2 at, std::string_view value, unsigned int colour,
              Align align, bool shadow, const std::string& fontDefinition,
              int textScale) const;
    // Measurement in a named face at a scale, for anything laying out around
    // text it did not draw.
    glm::ivec2 measureIn(std::string_view value, const std::string& fontDefinition,
                         int textScale) const;
    // A keyboard/mouse binding, drawn as a pressed cap: filled plate, border,
    // label. `textAt` is the same top-left a text() call would take, so a cap
    // and the words beside it line up by construction. Returns the plate width.
    //
    // It is a primitive rather than a string convention because a binding
    // written into prose ("` console  ESC quit") stops reading as a key at
    // all: punctuation bindings vanish into the sentence and the eye has no
    // column to scan. Every surface that shows a binding goes through here.
    int keyCap(glm::ivec2 textAt, std::string_view label,
               float alpha = 1.0f) const;
    int keyCapWidth(std::string_view label) const;
    // The plate is sized from the glyph *cell*, not from lineHeight: the cell
    // is taller, and a plate cut to a line height clips the letters inside it.
    int keyCapHeight() const { return mFont.cellHeight() - 1; }
    // Vertical pitch for a stacked column of caps -- taller than a text line,
    // because plates need a gap or the column reads as one long box.
    int keyCapRow() const { return keyCapHeight() + 2; }

    void bar(glm::ivec2 at, glm::ivec2 size, float ratio, unsigned int fill,
             unsigned int track) const;
    void icon(glm::ivec2 at, glm::ivec2 size, unsigned int colour,
              int inset = 0) const;

    // A window pixel as a virtual one -- the inverse of the mapping begin()
    // and beginTarget() set up. Here rather than at the call site because the
    // two forms differ (a full-window canvas has no origin, an embedded one
    // does) and a caller that inverted it by hand would be right for one of
    // them and quietly wrong for the other.
    glm::ivec2 toVirtual(glm::vec2 windowPixels) const;

    glm::ivec2 measure(std::string_view value) const {
        return mFont.measure(value);
    }
    int lineHeight() const { return mFont.lineHeight(); }

private:
    glm::vec2 toScreen(glm::ivec2 at) const;
    ImDrawList* list() const;
    void pushClip(ImDrawList* draw) const;
    void popClip(ImDrawList* draw) const;

    BitmapFont mFont;
    // Mutable because fontFor() is a const accessor that loads on demand: the
    // alternative is every caller pre-registering the faces its screen uses,
    // which is a rule somebody forgets and the symptom is missing text.
    mutable std::unordered_map<std::string, std::unique_ptr<BitmapFont>> mFonts;
    UiStyleSheet mStyle;
    glm::ivec2 mVirtual{320, 240};
    glm::vec2 mDisplay{0.0f};
    glm::vec2 mOrigin{0.0f};
    glm::vec2 mFramebufferScale{1.0f};
    int mScale = 1;
    // Null is imgui's foreground list, which is what a game HUD wants; a panel
    // passes its own so the canvas clips and z-orders with it.
    ImDrawList* mTarget = nullptr;
    bool mClipToTarget = false;
};

} // namespace eng::ui
