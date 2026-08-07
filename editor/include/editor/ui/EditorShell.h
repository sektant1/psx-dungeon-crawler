#pragma once

namespace game::content {
enum class SceneKind;
}

namespace ed {

// The shell: the frame the panels sit inside, and the arithmetic that places
// it.
//
// Everything here is pure -- no ImGui, no editor state -- for the same reason
// EditorWorkspace is: the top bar's three zones and the bottom panel's height
// are rules, and a rule that can only be exercised by opening the editor and
// looking at it is a rule that quietly stops holding. The drawing itself stays
// in EditorApp, where the state it reads lives.
//
// The arrangement is Godot's, deliberately and in full: menus at the top left,
// the main-screen switcher centred, the play controls at the top right, open
// scenes as tabs beneath them, and a bottom panel that is a strip of buttons
// until you click one. Authors arrive knowing where things are.

// --- main screen ------------------------------------------------------------
//
// What the centre of the window is showing. Godot's model: the switcher swaps
// the whole central editor rather than opening another dock, because "I am
// laying out a level" and "I am dressing a menu" are different jobs that want
// the same screen real estate.
//
// The three here are the three this editor already had -- the 3D level, the 2D
// page, and the material stage -- which until now were reached through a
// viewport enum, a docked panel and a menu item respectively, so nothing on
// screen said they were alternatives to each other.
enum class MainScreen {
    Scene3D,  // the level: kit pieces, gizmos, the grid
    Screen2D, // a ScreenCamera scene, and the game's HUD over it
    Material, // one sphere, one material, nothing else
};

inline constexpr int kMainScreenCount = 3;

const char* mainScreenName(MainScreen screen);
// The one-line "what is this for", shown on hover.
const char* mainScreenSummary(MainScreen screen);

// Which screen a scene of this kind should open in.
//
// This is the contract driving the workspace rather than the author driving it
// by hand: a 2D screen scene opened into the 3D level editor showed a page
// edge-on in a perspective view, and the fix -- switch to the HUD panel -- was
// something you had to already know.
MainScreen mainScreenForKind(game::content::SceneKind kind);

// --- top bar ----------------------------------------------------------------
//
// Three zones on one row. The middle one is centred on the *bar*, not on the
// space left over by the menus, so the switcher does not drift sideways as menu
// names change -- but it gives way rather than overlapping when the window is
// too narrow to hold all three.
struct TopBarLayout {
    // Where the main-screen switcher starts.
    float switcherX = 0.0f;
    // Where the play controls start. Right-aligned, always.
    float playX = 0.0f;
    // False when the bar is too narrow to centre the switcher and it has been
    // pushed left against the menus instead. The caller draws the same widgets
    // either way; this exists so a test can say which case it is in.
    bool switcherCentred = true;
    // False when even the left-packed arrangement does not fit. The caller then
    // drops the switcher from the bar (it stays reachable from the View menu),
    // because a switcher drawn under the play controls is worse than no
    // switcher.
    bool switcherFits = true;
};

TopBarLayout layoutTopBar(float barWidth, float menusEndX, float switcherWidth,
                          float playWidth, float pad);

// --- bottom panel -----------------------------------------------------------
//
// Godot's: a row of buttons along the bottom edge, and clicking one grows a
// region above it. Clicking the same one again puts it away.
//
// Deliberately NOT a dock node. Output, Problems and the Timeline are read
// *against* the viewport -- you scrub a clip and watch the door move -- so they
// want the full width and a height the author sets once, which is exactly what
// a dock node's tab bar and drag handles keep taking away. Outside the
// dockspace they also cannot be dragged somewhere they make no sense, which is
// where every one of them ended up in the layout this replaces.
enum class BottomTab {
    Output,   // the shared engine console
    Problems, // validation, with the fixes
    Timeline, // eng::ClipPanel
    // What the running game said when a script broke. Beside Problems rather
    // than inside it because the two answer different questions: Problems is
    // what is wrong with the scene on disk, this is what went wrong when it
    // ran, and only one of them can be fixed without pressing play.
    Scripts,
};

inline constexpr int kBottomTabCount = 4;

const char* bottomTabName(BottomTab tab);

struct BottomPanel {
    // The open tab, or -1 for collapsed. An int rather than an optional because
    // it is drawn as a radio group and compared against a loop counter.
    int open = -1;
    // What the author dragged it to, in pixels. Kept across collapses: the
    // panel reopening at somebody else's height is the small annoyance that
    // makes a collapsible panel not worth collapsing.
    float height = 240.0f;

    bool isOpen() const { return open >= 0; }
    bool isOpen(BottomTab tab) const { return open == int(tab); }
    // Click on a tab button: open it, or close it if it was already open.
    void toggle(BottomTab tab)
    {
        open = (open == int(tab)) ? -1 : int(tab);
    }
    void show(BottomTab tab) { open = int(tab); }
    void close() { open = -1; }
};

// The height the panel actually gets this frame: zero when collapsed, and
// otherwise clamped so it can never eat the workspace above it or shrink below
// something readable.
//
// `windowHeight` is the space the dockspace and the panel share. The ceiling is
// a fraction of it rather than a constant: a bottom panel that is allowed two
// thirds of a 720p window has taken the viewport, and one capped at 400px on a
// 4K display is a slot.
float bottomPanelHeight(const BottomPanel& panel, float windowHeight,
                        float minimumPixels);

} // namespace ed
