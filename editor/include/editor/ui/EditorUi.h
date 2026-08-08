#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace ed::ui {

// The editor's own widget vocabulary: the two layouts every panel in it should
// be built out of, in one place so they cannot drift.
//
// Both exist for the same reason. Dear ImGui's defaults are built for a debug
// overlay -- a label to the RIGHT of its widget, each panel free-form -- and
// that is exactly wrong for a tool somebody works in for an hour. A column of
// fields whose names sit at ragged right edges cannot be scanned, and four
// asset tabs that each invented their own arrangement meant the preview, the
// name and the search box were in a different place in each of them.

// --- property grid ---------------------------------------------------------
//
// Labels in a fixed left column, widgets filling the rest. The label column is
// a share of the panel rather than a constant: an inspector docked narrow has
// to give the widget room, and one dragged wide should not leave a metre of
// space between a name and its field.
//
//   ed::ui::PropertyGrid grid("camera");
//   grid.row("fov");            ImGui::SliderFloat("##fov", ...);
//   grid.row("near", "metres"); ImGui::DragFloat("##near", ...);
//
// Each row() opens the widget cell and sets the item width to fill it, so a
// caller only supplies the widget -- with a `##` label, because the name has
// already been drawn. Nesting is not supported and not wanted: a property grid
// inside a property grid is a sign the component should have been two.
class PropertyGrid
{
public:
    // `labelFraction` is the share of the width given to names, clamped to
    // something sane. The default suits component fields; a panel with long
    // names (the actor sound table) passes more.
    explicit PropertyGrid(const char* id, float labelFraction = 0.38f);
    ~PropertyGrid();

    PropertyGrid(const PropertyGrid&) = delete;
    PropertyGrid& operator=(const PropertyGrid&) = delete;

    // Opens a row and leaves the cursor in the widget cell. `units` is drawn
    // dimmed after the name ("(m)", "(deg)"); `hintId` gets the shared tooltip
    // table's entry when the name is hovered.
    void row(const char* label, const char* units = nullptr,
             const char* hintId = nullptr, const char* hintFallback = nullptr);
    // A row whose widget cell the caller leaves empty -- a note, a warning.
    void full(const char* text);

    bool open() const { return mOpen; }

private:
    bool mOpen = false;
};

// --- asset panel -----------------------------------------------------------
//
// The shape shared by every tab of the Asset Browser: a preview swatch with the
// subject's metadata beside it, the actions that apply the subject to the
// scene, a strip of view toggles, a search box, and the list.
//
// The order is the argument. An author's question in all four tabs is the same
// -- "what is this, and do I want it" -- so the answer belongs in the same place
// on screen in all four. Before this, Materials led with the swatch, Effects
// led with the list, and Placeables had no preview at all, so moving between
// them meant re-finding every control.
struct AssetPanelView {
    // The shared offscreen swatch, or 0 when the tab has no preview to show
    // (the space is then given to the metadata rather than left blank).
    std::uint64_t previewTexture = 0;
    const char* previewTooltip = nullptr;
    // Dragging on the swatch turns the subject. Null for a tab whose preview
    // does not rotate.
    std::function<void(float mouseDeltaX)> onPreviewDrag;

    // Beside the swatch: what the subject is called and what it is. Kept to a
    // few dimmed lines -- this is identification, not editing.
    std::function<void()> metadata;
    // Under the metadata: what this tab can do with the subject. Buttons.
    std::function<void()> actions;
    // Above the search box: how the list is shown. Checkboxes and radio rows.
    std::function<void()> toggles;

    // The search box writes straight into the caller's buffer.
    char* filter = nullptr;
    std::size_t filterCapacity = 0;
    const char* filterHint = "Search...";

    // The scrolling list. Drawn inside a child window that takes the rest of
    // the panel, so every tab's list ends at the same place.
    std::function<void()> list;

    // Shown dimmed at the foot: "97 meshes | 12 hidden by filter".
    std::string footer;
};

void drawAssetPanel(const AssetPanelView& view);

// How the preview block lays out at a given panel width. Pure, so the rule can
// be tested without a context: the interesting cases are the extremes, and a
// docked panel is dragged to both.
struct AssetPreviewLayout {
    float thumbnailSize = 96.0f;
    // False in a narrow panel, where the metadata goes under the swatch instead
    // of beside it. A 96-pixel image and three lines of text do not fit in 220
    // pixels, and forcing them produced a column of single words.
    bool metadataBeside = true;
    // The height the preview block occupies, whatever it contains.
    //
    // It used to be whatever the content came to, and that is what made the
    // panel restless: a texture with four metadata lines pushed the list down
    // relative to one with two, so arrowing through a list moved every row
    // under the cursor and the selection appeared to jump. Reserving the space
    // costs a little of it on the sparse subjects and makes the list a fixed
    // target -- which is what a browser is for.
    //
    // Content taller than this scrolls within the block rather than growing it.
    float headerHeight = 0.0f;
};

AssetPreviewLayout assetPreviewLayout(float availableWidth);

// --- metrics ---------------------------------------------------------------
//
// Sizes derived from the current style, for the places a raw pixel count would
// otherwise go.
//
// These exist because the editor has a UI scale setting that hardcoded sizes
// quietly opt out of. applyUiScale calls ImGui::ScaleAllSizes and sets
// FontGlobalScale, so padding, spacing and text all grow -- but an
// `ImVec2(120.0f, 0.0f)` button stays 120 pixels wide while the label inside it
// gets bigger, and at 1.5x the text runs out of its own button. Every size here
// is a multiple of something the style owns, so it follows the scale for free.

// The width of a dialog's confirm/cancel buttons. Wide enough to read as a
// button rather than as its text, equal across a dialog so the pair lines up.
float dialogButtonWidth();

// A square button that holds one glyph: the remove "x" at the end of a row, an
// arrow. Exactly the height of a frame, so it matches the widget beside it.
float iconButtonSize();

// Centres the next modal on the viewport and gives it a starting size and a
// floor, both in units of the CURRENT text height rather than pixels.
//
// Call immediately before BeginPopupModal. The sizes are `ImGuiCond_Appearing`,
// so an author who resizes a dialog keeps their size; this only decides what it
// opens at. The two dialogs that did this by hand each carried four pixel
// constants and disagreed about them, and both shrank relative to their own
// text as the UI scale went up.
void centreNextModal(float widthInTextHeights, float heightInTextHeights,
                     float minWidthInTextHeights,
                     float minHeightInTextHeights);

// --- rows that do not fall off the edge -------------------------------------
//
// Dear ImGui clips whatever runs past a window's right edge and, unless the
// window asked for a horizontal scrollbar, gives no way to reach it. A row of
// buttons built with SameLine is therefore a row that silently loses its last
// button when the panel is docked narrow -- and the button you cannot see is
// the one you do not have.
//
// Two answers, and the order matters. Content that *can* wrap should wrap,
// because a scrollbar is a second gesture before you can even see the control.
// Content that cannot -- a wide table, a long path -- gets the scrollbar, so
// nothing is ever unreachable. Every dockable panel in the editor now carries
// ImGuiWindowFlags_HorizontalScrollbar as the backstop; these helpers are what
// keeps it from being needed.

// Whether a run `nextWidth` wide still fits on a row whose cursor is at
// `cursorX`, inside a content region `contentWidth` wide.
//
// Pure, so the rule can be exercised without a context: the interesting cases
// are the boundaries, and a docked panel is dragged across all of them. `spacing`
// is the gap that would be inserted before the run.
bool rowHasRoom(float cursorX, float nextWidth, float contentWidth,
                float spacing);

// Continue the current row when the next run fits, otherwise start a new one.
// Not calling SameLine is what starts a new row, so "does not fit" is silence.
// Returns true when the row was continued.
bool sameLineIfItFits(float nextWidth);

// Widths a run will occupy, measured from the live style rather than guessed --
// the editor has a UI scale setting that hardcoded pixel counts opt out of.
float buttonWidth(const char* label);
float buttonRowWidth(const char* const* labels, std::size_t count);
float iconRowWidth(float iconSize, int count);

// --- shared bits -----------------------------------------------------------

// Case-insensitive substring match, the rule every search box in the editor
// uses. Empty query matches everything.
bool filterMatches(std::string_view text, std::string_view query);

// A byte count as "1.2 MB". For asset metadata, where the exact number is never
// the question.
std::string humanBytes(unsigned long long bytes);

} // namespace ed::ui
