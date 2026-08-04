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
};

AssetPreviewLayout assetPreviewLayout(float availableWidth);

// --- shared bits -----------------------------------------------------------

// Case-insensitive substring match, the rule every search box in the editor
// uses. Empty query matches everything.
bool filterMatches(std::string_view text, std::string_view query);

// A byte count as "1.2 MB". For asset metadata, where the exact number is never
// the question.
std::string humanBytes(unsigned long long bytes);

} // namespace ed::ui
