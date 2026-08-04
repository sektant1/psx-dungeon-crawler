#pragma once
#include <editor/scene/OutlinerTree.h>

#include <functional>
#include <vector>

namespace ed {

// The outliner's rows, split from EditorApp so a headless ImGui test can click
// them.
//
// That split is not ceremony: the panel's first version queried the row with
// ImGui::IsItemClicked() *after* drawing the kind tag beside it, and those
// queries answer for the last item submitted -- the tag, which is not
// interactive. Group and leaf rows therefore never registered a click, and a
// single-entity leaf has no children to click instead, so those entities could
// not be selected at all. A test that presses the mouse over a row is the only
// thing that catches that class of bug; the compiler cannot.

// What the pointer did to a row, in the terms every list in every tool uses.
//
// Ctrl toggles one row; Shift takes everything between the last click and this
// one. The panel used to map Shift onto toggle and have no range at all, which
// meant selecting a run of thirty rows was thirty clicks -- and the muscle
// memory of every other application selected the wrong thing on the way.
enum class SelectMode {
    Replace, // plain click
    Toggle,  // Ctrl
    Range,   // Shift
};

// Rows in the order the panel drew them last frame, and how far they were
// indented. Range selection needs to know what lies *between* two rows, and a
// row's position is only known once it is drawn -- by which time the row that
// starts the range has been submitted and the one that ends it has not.
//
// So the caller keeps this across frames and hands it back. Rows are stable
// between frames (the tree is rebuilt only on a document revision), so last
// frame's order is this frame's order, which is the same trade every immediate
// mode list makes.
struct OutlinerRowOrder {
    std::vector<game::content::AuthorId> ids;

    // Every id from `a` to `b` inclusive, in panel order. Empty if either is
    // absent -- a range against a row that is no longer drawn selects nothing
    // rather than guessing.
    std::vector<game::content::AuthorId>
    between(const game::content::AuthorId& a,
            const game::content::AuthorId& b) const;
};

struct OutlinerActions {
    std::function<bool(const game::content::AuthorId&)> isSelected;
    // Aggregate rows honor modifiers too: Ctrl toggles the group, Shift adds
    // it, plain click replaces. Passing the mode avoids reducing three intents
    // to the old ambiguous `add` boolean.
    std::function<void(const OutlinerGroup&, SelectMode)> selectGroup;
    std::function<void(const game::content::AuthorId&, bool add)> selectNode;
    // Ctrl/Shift-aware click on a single row. Null falls back to selectNode,
    // so a caller that has no anchor to track need not grow one.
    std::function<void(const game::content::AuthorId&, SelectMode,
                       const OutlinerRowOrder&)>
        clickNode;
    // Scroll to this row and open whatever is collapsed above it. Set when the
    // selection came from somewhere else -- a viewport pick, a command -- so
    // the panel follows the world instead of the author hunting for the row.
    // Empty means "nothing to reveal".
    game::content::AuthorId reveal;
    std::function<void()> focus; // the F key, and the menu's Focus item
    // Double-click: take the viewport into this entity and its descendants.
    // Null falls back to `focus`, which is what the gesture used to do and what
    // a caller with no isolation mode still wants.
    std::function<void(const game::content::AuthorId&)> isolate;
    std::function<void()>
        contextMenu; // draws the menu items, popup already open
    // Dragging a row onto another parents it there; an empty `parent` detaches
    // it. Null when the caller does not support reparenting, in which case the
    // rows are not drag sources either -- a drag that can never be dropped is
    // worse than no drag at all.
    std::function<void(const game::content::AuthorId& child,
                       const game::content::AuthorId& parent)>
        reparent;

    // Per-row visibility and lock. Null on either pair leaves that column out,
    // which is what a caller that has no such state wants -- an eye that does
    // nothing is worse than no eye.
    std::function<bool(const game::content::AuthorId&)> isHidden;
    std::function<void(const game::content::AuthorId&, bool)> setHidden;
    std::function<bool(const game::content::AuthorId&)> isLocked;
    std::function<void(const game::content::AuthorId&, bool)> setLocked;

    // -1 keeps current state, 0 collapses every aggregate, 1 expands it. The
    // caller sends a one-frame request from hierarchy header controls.
    int forceOpen = -1;
};

// Draws every group and its rows into the current window. `filterActive` forces
// the groups open, which is what a search result should do. `order` is
// rewritten with this frame's row order and must be the same object each frame
// -- see OutlinerRowOrder.
void drawOutlinerRows(const OutlinerTree& tree, bool filterActive,
                      const OutlinerActions& actions, OutlinerRowOrder& order);

// Overload for callers with no range selection and no reveal: keeps a
// throwaway order of its own.
void drawOutlinerRows(const OutlinerTree& tree, bool filterActive,
                      const OutlinerActions& actions);

} // namespace ed
