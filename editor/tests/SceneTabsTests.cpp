#include <editor/app/SceneTabs.h>

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "SceneTabsTests: " << message << '\n';
        std::exit(1);
    }
}

static SceneTab named(const std::string& path)
{
    SceneTab tab;
    tab.path = path;
    return tab;
}

int main()
{
    // --- the invariant --------------------------------------------------------
    {
        SceneTabs tabs;
        require(tabs.size() == 1,
                "an editor always has an open scene, even at startup");
        require(tabs.active() == 0, "the one tab is the active one");
        require(tabs.current().path.empty(), "and it has never been saved");
    }

    // --- opening --------------------------------------------------------------
    {
        SceneTabs tabs;
        const std::size_t a = tabs.open(named("/scenes/a.scn"));
        const std::size_t b = tabs.open(named("/scenes/b.scn"));
        require(tabs.size() == 3, "opening appends");
        require(tabs.active() == b, "and raises what was opened");
        require(a == 1 && b == 2, "indices are in the order they were opened");

        require(tabs.indexOfPath("/scenes/a.scn") == a,
                "a file already open is found rather than loaded twice");
        require(tabs.indexOfPath("/scenes/nope.scn") == tabs.size(),
                "a file that is not open reports size()");
        // Two untitled scenes are two scenes. Matching them on their shared
        // empty path would make the second one "already open".
        require(tabs.indexOfPath("") == tabs.size(),
                "an unsaved tab is never the same file as anything");
    }

    // --- identity survives its neighbours ------------------------------------
    {
        SceneTabs tabs;
        tabs.open(named("/a.scn"));
        tabs.open(named("/b.scn"));
        tabs.open(named("/c.scn"));

        std::set<unsigned long long> uids;
        for (std::size_t i = 0; i < tabs.size(); ++i)
            uids.insert(tabs.at(i).uid);
        require(uids.size() == tabs.size(), "every tab has its own uid");
        require(uids.count(0) == 0, "and none of them is the default zero");

        const unsigned long long c = tabs.at(3).uid;
        tabs.close(1); // the one holding /a.scn
        require(tabs.at(2).uid == c,
                "closing a tab does not renumber the survivors");
    }

    // --- closing --------------------------------------------------------------
    {
        SceneTabs tabs;
        tabs.open(named("/a.scn"));
        tabs.open(named("/b.scn"));
        tabs.activate(1); // /a.scn
        require(tabs.current().path == "/a.scn", "activate raises by index");

        // Closing something to the left must not move the author's document
        // out from under them.
        tabs.close(0);
        require(tabs.current().path == "/a.scn",
                "closing a tab to the left keeps the same scene active");

        // Closing the active one lands on its left neighbour, which is what
        // every tabbed editor does and what nobody has to be told.
        // The set is now [/a.scn, /b.scn]; close the second while it is active.
        tabs.activate(1);
        require(tabs.current().path == "/b.scn", "on /b.scn");
        tabs.close(1);
        require(tabs.current().path == "/a.scn",
                "closing the active tab lands on its left neighbour");
    }
    {
        // The same rule for a tab in the MIDDLE, which is where it used to
        // break: without a decrement, mActive kept its index and after the
        // erase that index is the tab to the RIGHT. The last-tab case above
        // passed anyway, because the trailing clamp happened to cover it.
        SceneTabs tabs;
        tabs.current().path = "/a.scn";
        tabs.open(SceneTab{});
        tabs.current().path = "/b.scn";
        tabs.open(SceneTab{});
        tabs.current().path = "/c.scn";

        tabs.activate(1);
        require(tabs.current().path == "/b.scn", "on the middle tab");
        tabs.close(1);
        require(tabs.size() == 2, "one closed");
        require(tabs.current().path == "/a.scn",
                "closing a middle active tab lands on its LEFT neighbour, "
                "not the one that slid into its index");
    }
    {
        SceneTabs tabs;
        tabs.current().path = "/only.scn";
        tabs.current().dirty = true;
        const bool blanked = tabs.close(0);
        require(blanked, "closing the last tab reports that it was replaced");
        require(tabs.size() == 1, "there is still exactly one tab");
        require(tabs.current().path.empty() && !tabs.current().dirty,
                "and it is a fresh, clean, untitled scene");
        require(tabs.current().uid != 0,
                "the replacement is a new tab, not the old one reused");
    }
    {
        SceneTabs tabs;
        tabs.open(named("/a.scn"));
        const std::size_t before = tabs.size();
        tabs.close(99);
        tabs.activate(99);
        require(tabs.size() == before && tabs.active() == 1,
                "an out-of-range index changes nothing");
    }

    // --- unsaved work ---------------------------------------------------------
    {
        SceneTabs tabs;
        require(!tabs.anyDirty(), "a fresh set has nothing to lose");
        tabs.open(named("/a.scn"));
        tabs.at(0).dirty = true;
        require(tabs.anyDirty(),
                "unsaved work in a background tab still counts -- which is the "
                "whole reason quitting asks about all of them");
    }

    // --- labels ---------------------------------------------------------------
    {
        SceneTab tab = named("/home/x/scenes/crypt.scn");
        require(sceneTabName(tab) == "crypt.scn",
                "a saved scene is labelled with its filename");
        require(sceneTabTooltip(tab) == "/home/x/scenes/crypt.scn",
                "and its tooltip is the full path");

        SceneTab fresh;
        require(sceneTabName(fresh) == "[unsaved]",
                "a scene with no file says so");
        require(sceneTabTooltip(fresh).find("never saved") != std::string::npos,
                "and its tooltip explains rather than showing an empty path");
    }

    std::cout << "SceneTabsTests: ok\n";
    return 0;
}
