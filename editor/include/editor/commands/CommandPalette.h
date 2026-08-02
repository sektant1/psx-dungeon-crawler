#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ed {

// The editor's actions, reachable by typing their name.
//
// The editor grew a menu bar, a toolbar, eight panels and thirty keybinds, and
// the cost of that is that a verb is only reachable if the author remembers
// *where* it lives. A palette collapses all of it into one question -- what do
// you want to do -- which is also the only surface a new verb can be added to
// for free: registering an action makes it findable, with no menu to place it
// in and no key left to bind.
//
// The action list is rebuilt from the editor's own state each time the palette
// opens (a piece the kit does not have must not be listed, and "Undo" must
// carry the label of the edit it would undo), so an action is a plain callable
// with the strings needed to find and read it.
struct PaletteAction {
    std::string label;    // what the author types and reads: "Cook scene"
    std::string group;    // "file", "edit", "place", ... also matched against
    std::string shortcut; // the keybind, shown right-aligned; may be empty
    std::string detail;   // one line under the label; may be empty
    std::function<void()> run;
    bool enabled = true;
};

struct PaletteMatch {
    std::size_t index = 0; // into the action list handed to matchPalette
    int score = 0;
};

// Ranks `actions` against `query`, best first, dropping what does not match.
//
// Matching is case-insensitive and subsequence-based -- "cksc" finds "Cook
// scene" -- because the point of typing is to stop before the whole word. A
// contiguous hit outranks a scattered one and a hit on a word start outranks a
// hit inside a word, so the thing the author half-remembered stays on top
// instead of losing to a long name that happens to contain the same letters.
//
// An empty query returns everything in the order given: the palette opened with
// no query is a menu of everything, and reordering that by anything other than
// registration would make the list move under the cursor.
std::vector<PaletteMatch> matchPalette(const std::vector<PaletteAction>& actions,
                                       const std::string& query);

// Everything the popup remembers between frames. Owned by the editor so the
// palette itself stays a function of (state, actions).
struct PaletteState {
    bool open = false;
    char query[96] = {};
    int highlighted = 0; // index into the *matches*, not the actions
    // Set on the frame the palette opens: the input box has to be focused once,
    // and re-focusing it every frame would eat the arrow keys.
    bool focusInput = false;
    std::string message; // why the highlighted action did not run
};

// Opens the palette on the next draw, cleared and focused.
void openPalette(PaletteState& state);

// Draws the popup and runs the chosen action. Returns true when an action ran,
// which is also when the palette closed.
//
// Actions are run *after* the popup is closed, so an action that opens another
// popup -- Save As, the discard prompt -- is not fighting this one for the
// modal stack.
bool drawCommandPalette(PaletteState& state,
                        const std::vector<PaletteAction>& actions);

} // namespace ed
