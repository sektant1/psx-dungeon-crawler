// Ranking for the editor's command palette.
//
// The property that matters: whatever the author half-remembered has to be the
// first row, because a palette is only faster than the menu bar if Enter can be
// pressed without reading the list. Everything here is a case where a naive
// substring filter puts the wrong thing on top.

#include <editor/commands/CommandPalette.h>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorCommandPaletteTests: " << message << '\n';
        std::exit(1);
    }
}

static PaletteAction action(std::string label, std::string group = "edit",
                            bool enabled = true)
{
    PaletteAction a;
    a.label = std::move(label);
    a.group = std::move(group);
    a.enabled = enabled;
    a.run = [] {};
    return a;
}

// The label of the best match, or "" when nothing matched.
static std::string best(const std::vector<PaletteAction>& actions,
                        const std::string& query)
{
    const std::vector<PaletteMatch> matches = matchPalette(actions, query);
    return matches.empty() ? std::string() : actions[matches.front().index].label;
}

int main()
{
    const std::vector<PaletteAction> actions = {
        action("Cook scene", "file"),
        action("Save scene", "file"),
        action("Save scene as...", "file"),
        action("Undo", "edit"),
        action("Redo", "edit"),
        action("Duplicate selection", "edit"),
        action("Delete selection", "edit"),
        action("Focus selection", "view"),
        action("Toggle snap to grid", "view"),
        action("Place kit.wall_arch", "place"),
        action("Place kit.floor_plain", "place"),
    };

    // --- an empty query is the whole menu, in the order given ---------------
    {
        const std::vector<PaletteMatch> all = matchPalette(actions, "");
        require(all.size() == actions.size(), "an empty query drops nothing");
        for (std::size_t i = 0; i < all.size(); ++i)
            require(all[i].index == i,
                    "and does not reorder -- the list must not move under the "
                    "cursor before a key is pressed");
    }

    // --- initials find the verb ---------------------------------------------
    require(best(actions, "cs") == "Cook scene",
            "word starts outrank letters buried inside a longer name");
    require(best(actions, "ds") == "Duplicate selection",
            "and the first registered wins a tie, not an arbitrary one");

    // --- typing a prefix is enough ------------------------------------------
    require(best(actions, "und") == "Undo", "a prefix of the name");
    require(best(actions, "UNDO") == "Undo", "matching is case-insensitive");
    require(best(actions, "cook") == "Cook scene", "a whole word");

    // --- a shorter name wins when both contain the query ---------------------
    require(best(actions, "save scene") == "Save scene",
            "'Save scene' beats 'Save scene as...' for the exact phrase");

    // --- the group is searchable, but never beats the name -------------------
    {
        const std::vector<PaletteMatch> place = matchPalette(actions, "place");
        require(!place.empty(), "a group name finds its actions");
        require(actions[place.front().index].label.rfind("Place", 0) == 0,
                "but an action whose *name* is Place... sorts above the ones "
                "merely filed under the place tool");
    }

    // --- gaps are allowed, nonsense is not -----------------------------------
    require(best(actions, "kwal") == "Place kit.wall_arch",
            "letters in order across a name find a kit piece");
    require(matchPalette(actions, "zzq").empty(),
            "letters that do not appear in order match nothing");
    require(matchPalette(actions, "oduns").empty(),
            "and order matters: the same letters scrambled are not a match");

    // --- spaces in the query are noise, not a requirement --------------------
    require(best(actions, "c s") == "Cook scene",
            "a space typed mid-search does not have to appear in the name");

    // --- a disabled action still appears, but yields to a live one -----------
    {
        const std::vector<PaletteAction> mixed = {
            action("Undo", "edit", false),
            action("Undo history", "edit", true),
        };
        const std::vector<PaletteMatch> matches = matchPalette(mixed, "undo");
        require(matches.size() == 2,
                "a greyed action is still listed -- being told why something "
                "is unavailable beats it vanishing");
        require(mixed[matches.front().index].label == "Undo history",
                "but an action that can actually run is offered first");
    }

    // --- degenerate inputs ---------------------------------------------------
    {
        require(matchPalette({}, "anything").empty(), "no actions, no matches");
        require(matchPalette(actions, "                ").size() ==
                    actions.size(),
                "a query of only spaces is an empty query");
    }

    std::cout << "EditorCommandPaletteTests: ok\n";
    return 0;
}
