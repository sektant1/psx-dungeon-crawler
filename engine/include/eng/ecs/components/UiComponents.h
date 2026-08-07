#pragma once
#include <eng/Reflect.h>

#include <glm/glm.hpp>

#include <string>

// Screen-space UI, as entities.
//
// WHY THIS EXISTS
// Every surface in this engine -- the HUD, the tooltip, the target banner -- is
// laid out in C++ against `eng::ui::UiCanvas`. That is fine for a HUD, which is
// four widgets that never move, and it is why the editor's 2D viewport
// documented itself as "not a WYSIWYG layout editor". It stops being fine the
// moment a game needs an inventory screen, a shop and a conversation: those are
// dozens of boxes whose positions are a design question, and answering a design
// question by editing a `.cpp` and waiting for a link is how UI work stops
// happening.
//
// So a screen is a scene. The same cooker, the same `.scn`, the same inspector,
// the same instancing -- a panel is an entity with components, and the editor
// edits it the way it edits everything else.
//
// WHAT IS DELIBERATELY NOT HERE
//   - No flexbox, no constraint solver, no stylesheet cascade. Anchors plus
//     offsets is the model Unity's RectTransform uses and it covers every
//     layout this game has; a solver is a large amount of machinery to make
//     "put it 8 pixels from the bottom-left" harder to predict.
//   - No input focus or event routing on the components. Which widget is
//     selected is gameplay state, and a screen that stored it would be a screen
//     that cannot be reloaded without losing the player's place.
//   - No colour values. Tones name a role in the active palette (see
//     `eng::ui::UiTone`), so a theme swap does not touch a single entity.
namespace eng::ecs {

// The layout box, and the one component every UI entity must carry.
//
// The rect is resolved against the parent's *resolved* rect:
//
//     min = parent.min + parent.size * anchorMin + offsetMin
//     max = parent.min + parent.size * anchorMax + offsetMax
//
// which makes the two common cases both natural. Equal anchors give a
// fixed-size box pinned to one spot (anchorMin = anchorMax = {0,1}, offsets in
// pixels from the bottom-left). Spread anchors give a box that stretches with
// its parent (anchorMin {0,0}, anchorMax {1,1}, offsets as inset margins).
//
// Units are *virtual* pixels -- the canvas's own grid, not the window's -- so a
// layout authored once is pixel-exact at every window size and integer scale.
struct UiRect {
    glm::vec2 anchorMin{0.0f, 0.0f};
    glm::vec2 anchorMax{0.0f, 0.0f};
    glm::vec2 offsetMin{0.0f, 0.0f};
    glm::vec2 offsetMax{120.0f, 24.0f};
    // Paint order among siblings, low first. Ties break on scene order, so a
    // screen that never sets this still draws in the order it was authored.
    int order = 0;
    // Hides this entity *and everything under it*: a screen is switched on and
    // off by one flag on its root, which is what makes screens cheap to author
    // as siblings in one scene.
    bool visible = true;
};

// A filled or framed plate. The chrome every other widget sits on.
struct UiPanel {
    // eng::ui::PanelStyle: 0 Solid, 1 Frame, 2 Sunken. An int rather than the
    // enum because the reflection layer's vocabulary is (bool, int, float,
    // vec2, vec3, colour, string) -- adding an enum type would mean teaching
    // the serialiser, the inspector, the cooker and Lua what an enum is, for
    // four values. The range is what keeps the inspector honest.
    int style = 0;
    // eng::ui::UiTone for the rail, when there is one.
    int rail = 0;      // eng::ui::RailEdge: 0 None, 1 Left, 2 Right, 3 Bottom
    int railTone = 2;  // Focus
    float opacity = 1.0f;
};

// Text. `binding` wins over `text` when it resolves, which is what makes a
// label show a live number without a script: the screen author writes
// "inventory.weight" and the game answers it.
struct UiLabel {
    std::string text;
    std::string binding;
    int tone = 0;   // eng::ui::UiTone: 0 Text
    int align = 0;  // eng::ui::Align: 0 Left, 1 Centre, 2 Right
    bool shadow = true;
    // A metrics file in assets/fonts. Empty is the canvas's own face, which is
    // what almost every label wants: a screen where each line picks its own
    // typeface is a screen nobody designed.
    std::string font;
    // Integer magnification of that face, multiplying the canvas scale -- so a
    // 2 is twice the size at every window size rather than at one of them.
    int textScale = 1;
    // An explicit colour, used instead of `tone` when `useColour` is set.
    //
    // Tones exist so a theme swap touches no entity, and they remain the right
    // default. This is the escape hatch for the cases a palette role cannot
    // express -- a faction's own colour, a one-off warning -- and it is opt-in
    // precisely so that using it is a decision rather than an accident.
    glm::vec3 colour{1.0f, 1.0f, 1.0f};
    bool useColour = false;
};

// A proportional gauge. `ratio` is the authored value; `binding` replaces it at
// runtime, so the same entity is a mock-up in the editor and a live bar in the
// game.
struct UiBar {
    float ratio = 1.0f;
    std::string binding;
    int fillTone = 3;  // Positive
    int trackTone = 7; // Edge
    glm::vec3 fillColour{1.0f, 1.0f, 1.0f};
    bool useFillColour = false;
};

// A solid chip. The cheapest way to mark a slot, a bullet or a status.
struct UiIcon {
    int tone = 2; // Focus
    int inset = 0;
    glm::vec3 colour{1.0f, 1.0f, 1.0f};
    bool useColour = false;
};

// A repeating row of live data: an inventory, a shop shelf, a quest log.
//
// THE HONEST LIMIT: this is a list *widget*, not a repeater. It asks the data
// source for rows and draws them with its own styling; it does not clone a
// template child per row. A real repeater needs a per-row binding scope, which
// is a much larger feature, and every list this game has is one line of text
// with an optional value and an optional gauge. `rowHeight` and the tones are
// the seam if that ever stops being true.
struct UiList {
    // Data source key, e.g. "inventory.backpack" or "trade.stock".
    std::string source;
    // What activating a row does, by name: "buy", "sell", "reply", "seal".
    // Empty is a display-only list.
    //
    // Authored rather than derived from `source`, because the same source means
    // different things on different screens: the backpack is what you seal on
    // the inventory screen and what you sell on the trade screen. Deriving it
    // would make the two indistinguishable, and hard-coding the pair in C++
    // would put the screen layout back in code.
    //
    // The engine does not know these words. It carries the string; the
    // application maps it to a verb (see game/src/ui/UiScreens.cpp), so another
    // game's screens name their own actions and share none of these.
    std::string action;
    int maxRows = 8;
    float rowHeight = 11.0f;
    float rowGap = 1.0f;
    // Draws the value column right-aligned. Off for a list of plain lines.
    bool showValues = true;
    int tone = 0;         // row text
    int selectedTone = 2; // the row the source marks selected
    // The face rows are drawn in, and its magnification. A list is where these
    // matter most: row pitch and text size have to agree or the rows overlap,
    // and the pitch is authored right here beside them.
    std::string font;
    int textScale = 1;
};

} // namespace eng::ecs

namespace eng {
// Declared here, defined in ComponentRegistry.cpp, for the reason PortalParams
// gives: a translation unit that only *uses* the table would otherwise
// instantiate the primary template and fail at link.
template <> FieldSpan fieldsOf<ecs::UiRect>();
template <> FieldSpan fieldsOf<ecs::UiPanel>();
template <> FieldSpan fieldsOf<ecs::UiLabel>();
template <> FieldSpan fieldsOf<ecs::UiBar>();
template <> FieldSpan fieldsOf<ecs::UiIcon>();
template <> FieldSpan fieldsOf<ecs::UiList>();
} // namespace eng
