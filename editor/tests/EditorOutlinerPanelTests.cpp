// Clicking the outliner, with no window.
//
// ImGui runs headless: give it a display size and a font atlas, inject mouse
// events, and the widgets behave exactly as they do on screen. That is the only
// way to catch what broke here -- the panel asked IsItemClicked() *after*
// drawing the kind tag beside the row, so the query answered for the tag, and
// group and leaf rows silently stopped selecting anything.

#include <editor/ui/OutlinerPanel.h>

#include <imgui.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace ed;
using game::content::AuthorId;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorOutlinerPanelTests: " << message << '\n';
        std::exit(1);
    }
}

namespace {

// One draw of the panel. ImGui needs a frame to place the rows before a click
// can land on one, so every case runs at least twice: frame one to lay out,
// frame two with the mouse over the row it found.
struct Harness {
    OutlinerTree tree;
    std::vector<AuthorId> selection;
    int focusCalls = 0;
    // Row rectangles, captured while drawing, so a test can aim at one by name
    // instead of guessing at pixel offsets.
    std::vector<std::pair<std::string, ImVec2>> rowCentres;

    // The panel's row order from last frame, exactly as EditorApp keeps it.
    OutlinerRowOrder rows;
    AuthorId anchor;
    AuthorId reveal;

    OutlinerActions actions()
    {
        OutlinerActions a;
        a.reveal = reveal;
        if (modifierAware) {
            a.clickNode = [this](const AuthorId& id, SelectMode mode,
                                 const OutlinerRowOrder& order) {
                switch (mode) {
                case SelectMode::Replace:
                    selection.assign(1, id);
                    anchor = id;
                    break;
                case SelectMode::Toggle:
                    selection.push_back(id);
                    anchor = id;
                    break;
                case SelectMode::Range: {
                    const std::vector<AuthorId> run =
                        anchor.empty() ? std::vector<AuthorId>{}
                                       : order.between(anchor, id);
                    selection = run.empty() ? std::vector<AuthorId>{id} : run;
                    break;
                }
                }
            };
        }
        a.isSelected = [this](const AuthorId& id) {
            for (const AuthorId& selected : selection)
                if (selected == id)
                    return true;
            return false;
        };
        a.selectGroup = [this](const OutlinerGroup& group, bool add) {
            if (!add)
                selection.clear();
            for (const AuthorId& id : groupIds(group))
                selection.push_back(id);
        };
        a.selectNode = [this](const AuthorId& id, bool add) {
            if (!add)
                selection.clear();
            selection.push_back(id);
        };
        a.focus = [this] { ++focusCalls; };
        a.isHidden = [this](const AuthorId& id) {
            return std::find(hidden.begin(), hidden.end(), id) != hidden.end();
        };
        a.setHidden = [this](const AuthorId& id, bool on) {
            hidden.erase(std::remove(hidden.begin(), hidden.end(), id),
                         hidden.end());
            if (on)
                hidden.push_back(id);
        };
        a.isLocked = [this](const AuthorId& id) {
            return std::find(locked.begin(), locked.end(), id) != locked.end();
        };
        a.setLocked = [this](const AuthorId& id, bool on) {
            locked.erase(std::remove(locked.begin(), locked.end(), id),
                         locked.end());
            if (on)
                locked.push_back(id);
        };
        a.contextMenu = [] { ImGui::MenuItem("Delete"); };
        if (allowReparent) {
            a.reparent = [this](const AuthorId& child, const AuthorId& parent) {
                reparents.emplace_back(child, parent);
            };
        }
        return a;
    }

    std::vector<AuthorId> hidden;
    std::vector<AuthorId> locked;
    // Where the switch column starts, sampled inside the window during draw:
    // recomputing it from the window width in the test gets the padding wrong
    // and aims every click just off the icons.
    float toggleLeft = 0.0f;
    bool allowReparent = false;
    bool modifierAware = false;
    std::vector<std::pair<AuthorId, AuthorId>> reparents;
};

// Modifier held for the next click; ImGui reads it off the io flags.
bool gShift = false;
bool gCtrl = false;

void beginFrame(ImVec2 mouse, bool down)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;
    io.AddKeyEvent(ImGuiMod_Shift, gShift);
    io.AddKeyEvent(ImGuiMod_Ctrl, gCtrl);
    io.AddMousePosEvent(mouse.x, mouse.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(400.0f, 600.0f));
    ImGui::Begin("Outliner", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
}

void endFrame()
{
    ImGui::End();
    ImGui::Render();
}

// Draws the panel and records where each row landed.
void draw(Harness& harness)
{
    harness.rowCentres.clear();
    // Mirrors toggleColumnLeft() in the panel, evaluated in the same window.
    {
        const float size = ImGui::GetFontSize();
        const float gap = 6.0f;
        harness.toggleLeft = ImGui::GetWindowPos().x +
                             ImGui::GetWindowContentRegionMax().x -
                             ImGui::GetStyle().ScrollbarSize - (size + gap) * 2.0f;
    }
    OutlinerActions actions = harness.actions();
    const auto note = [&harness](const std::string& key) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        harness.rowCentres.emplace_back(
            key, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f));
    };
    // The panel does not expose row rects, so the harness re-walks the tree the
    // same way and samples the cursor: rows are laid out top to bottom at a
    // fixed line height, which is what makes this reliable.
    const float top = ImGui::GetCursorScreenPos().y;
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    drawOutlinerRows(harness.tree, false, actions, harness.rows);
    (void)note;
    float y = top + lineHeight * 0.5f;
    // Composed groups draw their whole subtree open, one row per entity, so the
    // walk has to descend or every row below the first would be mis-aimed.
    const auto descend = [&](auto&& self, const OutlinerNode& node) -> void {
        harness.rowCentres.emplace_back(node.id, ImVec2(120.0f, y));
        y += lineHeight;
        for (const OutlinerNode& child : node.children)
            self(self, child);
    };
    for (const OutlinerGroup& group : harness.tree.groups) {
        if (group.composed && !group.nodes.empty()) {
            descend(descend, group.nodes.front());
            continue;
        }
        harness.rowCentres.emplace_back(group.key, ImVec2(120.0f, y));
        y += lineHeight;
    }
}

ImVec2 rowCentre(const Harness& harness, const std::string& key)
{
    for (const auto& [name, centre] : harness.rowCentres)
        if (name == key)
            return centre;
    require(false, "row '" + key + "' was not drawn");
    return ImVec2();
}

// Lays the panel out, then clicks the named row: press on one frame, release on
// the next, which is what ImGui's click detection expects.
void clickRow(Harness& harness, const std::string& key)
{
    // Long frame with the pointer away: successive cases click the same pixel a
    // few frames apart, and ImGui would count that as a double click -- which a
    // group row answers by opening, not by selecting.
    ImGui::GetIO().AddMousePosEvent(-1.0f, -1.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    ImGui::GetIO().DeltaTime = 1.0f;
    ImGui::NewFrame();
    ImGui::Render();

    beginFrame(ImVec2(-1.0f, -1.0f), false);
    draw(harness);
    endFrame();

    const ImVec2 target = rowCentre(harness, key);
    beginFrame(target, false); // hover first: ImGui needs the row hot
    draw(harness);
    endFrame();

    beginFrame(target, true);
    draw(harness);
    endFrame();

    beginFrame(target, false);
    draw(harness);
    endFrame();
}

// Which switch on the row to aim at.
enum class RowSwitch { Eye, Lock };

// The same gesture as clickRow, but aimed at one of the two switches at the
// right-hand end of the row rather than at its name.
//
// The position is resolved after the layout frame, because the row rects only
// exist once the panel has drawn once.
void clickSwitch(Harness& harness, const std::string& key, RowSwitch which)
{
    ImGui::GetIO().AddMousePosEvent(-1.0f, -1.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    ImGui::GetIO().DeltaTime = 1.0f;
    ImGui::NewFrame();
    ImGui::Render();

    beginFrame(ImVec2(-1.0f, -1.0f), false);
    draw(harness);
    endFrame();

    const ImVec2 row = rowCentre(harness, key);
    const float size = ImGui::GetFontSize();
    const float gap = 6.0f;
    const float left = harness.toggleLeft;
    const float x = which == RowSwitch::Eye ? left + size * 0.5f
                                            : left + size + gap + size * 0.5f;
    const ImVec2 target(x, row.y);

    // Two hover frames: the row claims the hover on the first, and the switch
    // takes it over on the second. That handover IS the thing under test.
    beginFrame(target, false);
    draw(harness);
    endFrame();
    beginFrame(target, false);
    draw(harness);
    endFrame();

    beginFrame(target, true);
    draw(harness);
    endFrame();

    beginFrame(target, false);
    draw(harness);
    endFrame();
}

OutlinerGroup makeGroup(const std::string& key, const std::string& kind,
                        const std::vector<std::string>& ids)
{
    OutlinerGroup group;
    group.key = key;
    group.label = key;
    group.kind = kind;
    group.geometry = ids.size() > 1;
    for (const std::string& id : ids)
        group.nodes.push_back(OutlinerNode{id, id, kind});
    return group;
}

} // namespace

int main()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(400.0f, 600.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);

    // --- a leaf: one entity, no children to fall back on -------------------
    {
        Harness harness;
        harness.tree.groups.push_back(
            makeGroup("spawn", "spawn", {"spawn_0001"}));
        harness.tree.shown = 1;
        clickRow(harness, "spawn");
        require(harness.selection.size() == 1 &&
                    harness.selection[0] == "spawn_0001",
                "clicking a leaf row selects the entity it stands for");
    }

    // --- a group: the whole thing, in one click ----------------------------
    {
        Harness harness;
        harness.tree.groups.push_back(makeGroup(
            "kit.wall", "wall", {"wall_0001", "wall_0002", "wall_0003"}));
        harness.tree.shown = 3;
        clickRow(harness, "kit.wall");
        require(harness.selection.size() == 3,
                "clicking a group row selects every entity in it");
    }

    // --- a leaf below a group: the row under the cursor, not the first one --
    {
        Harness harness;
        harness.tree.groups.push_back(
            makeGroup("kit.wall", "wall", {"wall_0001", "wall_0002"}));
        harness.tree.groups.push_back(makeGroup("exit", "exit", {"exit_0001"}));
        harness.tree.shown = 3;
        clickRow(harness, "exit");
        require(harness.selection.size() == 1 &&
                    harness.selection[0] == "exit_0001",
                "a leaf under a collapsed group still selects itself");
    }

    // --- clicking nothing changes nothing ----------------------------------
    {
        Harness harness;
        harness.tree.groups.push_back(
            makeGroup("spawn", "spawn", {"spawn_0001"}));
        harness.tree.shown = 1;
        beginFrame(ImVec2(200.0f, 550.0f), false);
        draw(harness);
        endFrame();
        beginFrame(ImVec2(200.0f, 550.0f), true);
        draw(harness);
        endFrame();
        require(harness.selection.empty(),
                "clicking empty panel space selects nothing");
    }

    // --- a composed object: every row in the chain is its own click target --
    {
        Harness harness;
        OutlinerGroup group;
        group.key = "chandelier_0001";
        group.label = "chandelier_0001";
        group.kind = "prop";
        group.composed = true;
        OutlinerNode root{"chandelier_0001", "chandelier_0001", "prop", {}};
        root.children.push_back({"candle_0001", "candle_0001", "prop", {}});
        root.children.push_back({"candle_0002", "candle_0002", "prop", {}});
        group.nodes.push_back(root);
        harness.tree.groups.push_back(std::move(group));
        harness.tree.shown = 3;

        clickRow(harness, "chandelier_0001");
        require(harness.selection.size() == 1 &&
                    harness.selection[0] == "chandelier_0001",
                "clicking the root of a composed object selects the root -- not "
                "the whole chain, which is what the group row of a *prefab* "
                "group does");

        clickRow(harness, "candle_0002");
        require(harness.selection.size() == 1 &&
                    harness.selection[0] == "candle_0002",
                "and a child deep in the chain selects itself, which is the "
                "only way to adjust one candle");
    }

    // --- rows are only drag sources when the caller can act on a drop -------
    {
        Harness harness;
        harness.tree.groups.push_back(
            makeGroup("spawn", "spawn", {"spawn_0001"}));
        harness.tree.shown = 1;
        // No reparent handler: dragging must still select, not swallow clicks.
        clickRow(harness, "spawn");
        require(harness.selection.size() == 1,
                "a panel without reparenting still selects normally");
        require(harness.reparents.empty(), "and reports no reparenting");
    }

    // --- Shift takes the run between two rows -----------------------------
    // The gap this closes: Shift used to mean "toggle", so selecting a run of
    // thirty rows was thirty clicks, and every other application's muscle
    // memory did the wrong thing on the way.
    {
        Harness harness;
        harness.modifierAware = true;
        OutlinerGroup group;
        group.key = "chain";
        group.label = "chain";
        group.kind = "prop";
        group.composed = true;
        OutlinerNode root{"n0", "n0", "prop", {}, {}};
        root.children.push_back({"n1", "n1", "prop", {}, {}});
        root.children.push_back({"n2", "n2", "prop", {}, {}});
        root.children.push_back({"n3", "n3", "prop", {}, {}});
        group.nodes.push_back(root);
        harness.tree.groups.push_back(std::move(group));
        harness.tree.shown = 4;

        clickRow(harness, "n0");
        require(harness.selection.size() == 1 && harness.anchor == "n0",
                "a plain click sets the anchor");

        gShift = true;
        clickRow(harness, "n2");
        gShift = false;
        require(harness.selection.size() == 3,
                "shift-click takes every row from the anchor to here");
        require(harness.selection.front() == "n0" &&
                    harness.selection.back() == "n2",
                "and the run is in panel order, ends included");

        // A second range from the same anchor replaces the first rather than
        // unioning with it, which is the only way to shrink an overshoot.
        gShift = true;
        clickRow(harness, "n1");
        gShift = false;
        require(harness.selection.size() == 2,
                "a second shift-click re-ranges from the same anchor");

        // Backwards from the anchor is the same run.
        clickRow(harness, "n3");
        gShift = true;
        clickRow(harness, "n1");
        gShift = false;
        require(harness.selection.size() == 3 &&
                    harness.selection.front() == "n1",
                "a range that runs upward is still in panel order");
    }

    // --- Ctrl adds one row without losing the rest -------------------------
    {
        Harness harness;
        harness.modifierAware = true;
        harness.tree.groups.push_back(makeGroup("a", "prop", {"a0"}));
        harness.tree.groups.push_back(makeGroup("b", "prop", {"b0"}));
        harness.tree.shown = 2;
        clickRow(harness, "a");
        gCtrl = true;
        clickRow(harness, "b");
        gCtrl = false;
        require(harness.selection.size() == 2, "ctrl-click extends");
    }

    // --- a range with no anchor is just a click ----------------------------
    // Selecting from the top of the list instead would be a surprise measured
    // in hundreds of entities.
    {
        Harness harness;
        harness.modifierAware = true;
        harness.tree.groups.push_back(makeGroup("a", "prop", {"a0"}));
        harness.tree.groups.push_back(makeGroup("b", "prop", {"b0"}));
        harness.tree.shown = 2;
        gShift = true;
        clickRow(harness, "b");
        gShift = false;
        require(harness.selection.size() == 1 && harness.selection[0] == "b0",
                "a range with no anchor selects the one row");
    }

    // --- a caller without clickNode still selects the old way ---------------
    {
        Harness harness; // modifierAware stays false
        harness.tree.groups.push_back(makeGroup("spawn", "spawn", {"spawn_0001"}));
        harness.tree.shown = 1;
        clickRow(harness, "spawn");
        require(harness.selection.size() == 1,
                "a panel with no modifier handler falls back to selectNode");
    }

    // --- reveal opens a collapsed group so the row can be scrolled to -------
    // A row inside a folded group is not drawn at all, so scrolling to it is a
    // no-op unless the group is opened first.
    {
        Harness harness;
        harness.tree.groups.push_back(
            makeGroup("kit.wall", "wall", {"wall_0001", "wall_0002"}));
        harness.tree.shown = 2;
        harness.reveal = "wall_0002";
        beginFrame(ImVec2(-1.0f, -1.0f), false);
        draw(harness);
        endFrame();
        bool drawn = false;
        for (const AuthorId& id : harness.rows.ids)
            drawn = drawn || id == "wall_0002";
        require(drawn, "revealing a row inside a collapsed group opens it");
    }

    // --- between() is order-agnostic and safe on missing rows ---------------
    {
        OutlinerRowOrder order;
        order.ids = {"a", "b", "c", "d"};
        require(order.between("b", "d").size() == 3, "inclusive both ends");
        require(order.between("d", "b").size() == 3, "and symmetric");
        require(order.between("a", "a").size() == 1, "a row against itself");
        require(order.between("a", "zz").empty(),
                "a range against a row that is gone selects nothing");
    }

    // --- the eye and the padlock actually toggle ---------------------------
    //
    // Both were dead: the row spans the full width and is submitted first, so
    // it won the hit test everywhere including under the two switches, and the
    // InvisibleButtons behind them never saw a press. Nothing about that is
    // visible on screen -- the icons draw, they hover, and clicking does
    // nothing at all.
    {
        Harness harness;
        harness.tree.groups.push_back(makeGroup("spawn", "spawn", {"spawn_0001"}));

        clickSwitch(harness, "spawn", RowSwitch::Eye);
        require(harness.hidden.size() == 1 && harness.hidden.front() == "spawn_0001",
                "clicking the eye hides the row's entity");
        // The click belongs to the switch, not to the row underneath it.
        require(harness.selection.empty(),
                "and does not also select the row");
        require(harness.focusCalls == 0, "nor focus the camera on it");

        clickSwitch(harness, "spawn", RowSwitch::Eye);
        require(harness.hidden.empty(), "clicking it again shows the entity");

        clickSwitch(harness, "spawn", RowSwitch::Lock);
        require(harness.locked.size() == 1 && harness.locked.front() == "spawn_0001",
                "the padlock locks the row's entity");
        require(harness.selection.empty(), "without selecting it");

        clickSwitch(harness, "spawn", RowSwitch::Lock);
        require(harness.locked.empty(), "and unlocks it");

        // The row itself must still select -- the overlap fix must not have
        // handed the whole row to the switches.
        clickRow(harness, "spawn");
        require(harness.selection.size() == 1 &&
                    harness.selection.front() == "spawn_0001",
                "clicking the name still selects");
    }

    // --- a group's switches act on every entity under it --------------------
    {
        Harness harness;
        harness.tree.groups.push_back(
            makeGroup("kit.wall", "kit", {"wall_0001", "wall_0002", "wall_0003"}));

        clickSwitch(harness, "kit.wall", RowSwitch::Eye);
        require(harness.hidden.size() == 3,
                "hiding a group hides all of it -- a group of a hundred and "
                "sixty doors is the case this exists for");

        clickSwitch(harness, "kit.wall", RowSwitch::Eye);
        require(harness.hidden.empty(), "and shows all of it again");
    }

    ImGui::DestroyContext();
    std::cout << "EditorOutlinerPanelTests: ok\n";
    return 0;
}
