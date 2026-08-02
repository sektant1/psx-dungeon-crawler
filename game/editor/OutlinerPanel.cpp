#include "OutlinerPanel.h"

#include "EditorIcons.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace ed {
namespace {

ImVec4 kindColour(const std::string& kind)
{
    if (kind == "MISSING")
        return ImVec4(1.00f, 0.42f, 0.36f, 1.0f);
    if (kind == "spawn" || kind == "exit")
        return ImVec4(0.55f, 0.92f, 0.62f, 1.0f);
    if (kind == "enemy" || kind == "trigger")
        return ImVec4(1.00f, 0.68f, 0.45f, 1.0f);
    if (kind == "light" || kind == "sun")
        return ImVec4(0.98f, 0.88f, 0.45f, 1.0f);
    if (kind == "marker" || kind == "pickup")
        return ImVec4(0.68f, 0.78f, 1.00f, 1.0f);
    return ImVec4(0.60f, 0.63f, 0.70f,
                  1.0f); // kit geometry: the quiet majority
}

// Everything the row needs to know about the pointer, sampled the moment the
// row's own item is submitted. Anything drawn after it -- the kind tag, the
// label -- becomes "the last item", and the IsItem* family would then answer
// for that instead.
struct RowInput {
    bool clicked = false;
    bool doubleClicked = false;
    SelectMode mode = SelectMode::Replace;
};

RowInput sampleRow()
{
    RowInput input;
    const bool hovered = ImGui::IsItemHovered();
    input.clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
    input.doubleClicked =
        hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    // Shift beats Ctrl when both are down, which is what a file manager does:
    // the range is the bigger intent, and Ctrl+Shift meaning "add a range" is
    // covered because Range extends rather than replaces.
    const ImGuiIO& io = ImGui::GetIO();
    input.mode = io.KeyShift  ? SelectMode::Range
                 : io.KeyCtrl ? SelectMode::Toggle
                              : SelectMode::Replace;
    return input;
}

// Routes a single-row click through the modifier-aware handler when the caller
// has one, and through the old add/replace pair when it does not.
void applyClick(const game::content::AuthorId& id, SelectMode mode,
                const OutlinerActions& actions, const OutlinerRowOrder& order)
{
    if (actions.clickNode) {
        actions.clickNode(id, mode, order);
        return;
    }
    if (actions.selectNode)
        actions.selectNode(id, mode != SelectMode::Replace);
}

// Scrolls to the row just submitted, once, when it is the one the caller asked
// to reveal. Called after the row's item so GetItemRect is this row's.
void revealRow(const game::content::AuthorId& id, const OutlinerActions& actions)
{
    if (actions.reveal.empty() || actions.reveal != id)
        return;
    // Centred rather than merely visible: a row scrolled to the very bottom
    // edge is technically revealed and still reads as "not found".
    ImGui::SetScrollHereY(0.5f);
}

// The vertical line down a subtree, so a deep chain reads as a chain.
//
// ImGui indents children but draws nothing between them, and at this panel's
// docked width four levels of a chandelier are four nearly-identical
// left margins. The guide is what says which rows belong to which parent.
void drawIndentGuide(float topY)
{
    const float x = ImGui::GetCursorScreenPos().x -
                    ImGui::GetStyle().IndentSpacing * 0.5f;
    const float bottomY = ImGui::GetCursorScreenPos().y -
                          ImGui::GetStyle().ItemSpacing.y;
    if (bottomY <= topY)
        return;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(x, topY), ImVec2(x, bottomY),
        ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
}

// Where the eye and the padlock column starts, in screen x. Everything to the
// left of it is the label's, and everything drawn must stop there.
float toggleColumnLeft()
{
    const float size = ImGui::GetFontSize();
    const float gap = 6.0f;
    return ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x -
           ImGui::GetStyle().ScrollbarSize - (size + gap) * 2.0f;
}

// The kind's icon, then the name.
//
// The kind used to be seven characters of padded text before every name, which
// at the width this panel docks to was a quarter of the row spent saying
// something a shape says instantly -- and it pushed the names, the part that is
// actually scanned, off the right edge.
//
// The label is clipped to the toggle column rather than allowed to run under
// it: at this panel's docked width a long name reached the padlock and drew
// through it, so the row read as a name with a smudge on the end and the switch
// it hid could not be aimed at.
void drawKindLabel(const std::string& kind, const std::string& label,
                   bool dimmed, float rightLimit = 0.0f)
{
    const float size = ImGui::GetFontSize();
    ImGui::SameLine(0.0f, 4.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec4 colour = kindColour(kind);
    if (dimmed)
        colour.w *= 0.45f;
    drawIcon(ImGui::GetWindowDrawList(), iconForKind(kind.c_str()),
             ImVec2(origin.x, origin.y + 1.0f), size, ImGui::GetColorU32(colour));
    if (ImGui::IsMouseHoveringRect(origin,
                                   ImVec2(origin.x + size, origin.y + size)))
        ImGui::SetTooltip("%s", kind.c_str());
    ImGui::Dummy(ImVec2(size, size));
    ImGui::SameLine(0.0f, 6.0f);

    const float right = rightLimit > 0.0f ? rightLimit : toggleColumnLeft();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool clipped = ImGui::CalcTextSize(label.c_str()).x > right - at.x;
    if (clipped)
        ImGui::PushClipRect(at, ImVec2(right, at.y + size * 2.0f), true);
    if (dimmed)
        ImGui::TextDisabled("%s", label.c_str());
    else
        ImGui::TextUnformatted(label.c_str());
    if (clipped) {
        ImGui::PopClipRect();
        // The full name is still reachable, which is what makes clipping
        // acceptable rather than lossy.
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", label.c_str());
    }
}

// How many components this entity carries, as a small trailing count.
//
// A dense level is three hundred rows that look alike, and what actually
// distinguishes two identical pillars is that one of them has the trigger. The
// count says "there is more here than the name"; the tooltip says what, which
// is cheaper than spelling out four component names on every row at this
// panel's docked width.
void drawComponentBadge(const std::vector<std::string>& components)
{
    // One component is the thing the row already says it is (a light, a mesh):
    // a badge on every row is a column of noise. Two or more is the signal.
    if (components.size() < 2)
        return;
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextDisabled("%zu", components.size());
    if (!ImGui::IsItemHovered())
        return;
    std::string list;
    for (const std::string& id : components) {
        if (!list.empty())
            list += ", ";
        list += id;
    }
    ImGui::SetTooltip("%s", list.c_str());
}

// How many entities a group holds, right-aligned in its own column just left of
// the switches.
//
// It used to be glued to the label -- "kit.floor  (146)" -- and at this panel's
// docked width the count was the part that got clipped off, which is exactly
// backwards: the name is recognisable from its first eight characters and the
// count is not recoverable from anything. Its own column also puts every count
// on one vertical line, so the sizes of a level compare at a glance.
void drawGroupCount(std::size_t count)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%zu", count);
    const float width = ImGui::CalcTextSize(text).x;
    ImGui::SameLine();
    const ImVec2 here = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(
        ImVec2(std::max(here.x, toggleColumnLeft() - width - 8.0f), here.y));
    ImGui::TextDisabled("%s", text);
}

// The eye and the padlock, right-aligned on the row.
//
// Right-aligned because the names are the column being read and these are
// per-row switches: a list whose every line starts with two icons before the
// thing it names is a list you scan twice.
// `ids` is what the switch acts on: one entity for a leaf, the whole lot for a
// group row. A group of a hundred and sixty doors could not be hidden at all
// before -- only its members, one at a time, after opening it.
// True when the pointer is over the switch column on the row being drawn.
//
// The row and the switches resolve a click on *different frames*: a row reports
// IsItemClicked() when the button goes down, an InvisibleButton fires when it
// comes back up. So a return value from drawRowToggles cannot suppress the
// row's click -- by the time the switch fires, the row has already selected.
//
// The column is reserved instead, which is what the layout already assumes:
// drawKindLabel clips names at exactly this line so they cannot reach the
// switches. A click past it belongs to the switches, on any frame.
bool pointerInToggleColumn(const OutlinerActions& actions)
{
    if (!(actions.isHidden && actions.setHidden) &&
        !(actions.isLocked && actions.setLocked))
        return false; // no switches drawn: the whole row is the row's
    return ImGui::GetMousePos().x >= toggleColumnLeft();
}

// Returns true when a switch took this frame's click. Used together with the
// column test above: this covers the release frame, that one the press.
bool drawRowToggles(const std::vector<game::content::AuthorId>& ids,
                    const char* scope, const OutlinerActions& actions)
{
    const bool hasVisibility = actions.isHidden && actions.setHidden;
    const bool hasLock = actions.isLocked && actions.setLocked;
    if ((!hasVisibility && !hasLock) || ids.empty())
        return false;

    const float gap = 6.0f;
    ImGui::SameLine();
    const ImVec2 here = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(
        ImVec2(std::max(here.x, toggleColumnLeft()), here.y));

    // A group counts as hidden only when all of it is. Mixed reads as shown,
    // because the click that follows should hide the rest rather than reveal
    // the few.
    const auto all = [&ids](const auto& query) {
        for (const game::content::AuthorId& id : ids)
            if (!query(id))
                return false;
        return true;
    };
    const bool many = ids.size() > 1;

    bool consumed = false;
    ImGui::PushID(scope);
    if (hasVisibility) {
        const bool hidden = all(actions.isHidden);
        if (iconToggle(hidden ? Icon::EyeClosed : Icon::Eye, "##vis", !hidden,
                       many ? (hidden ? "all hidden -- click to show them"
                                      : "click to hide every entity in here")
                            : (hidden ? "hidden -- click to show"
                                      : "visible -- click to hide"))) {
            for (const game::content::AuthorId& id : ids)
                actions.setHidden(id, !hidden);
            consumed = true;
        }
        ImGui::SameLine(0.0f, gap);
    }
    if (hasLock) {
        const bool locked = all(actions.isLocked);
        if (iconToggle(locked ? Icon::Lock : Icon::Unlock, "##lock", locked,
                       many ? (locked ? "all locked -- click to unlock them"
                                      : "click to lock every entity in here")
                            : (locked ? "locked -- cannot be picked in the viewport"
                                      : "unlocked -- click to stop picking it"))) {
            for (const game::content::AuthorId& id : ids)
                actions.setLocked(id, !locked);
            consumed = true;
        }
    }
    ImGui::PopID();
    return consumed;
}

// The payload id is fixed so a drag started on any row can be dropped on any
// other. It carries the author id as text rather than a pointer: the document
// is edited by the drop, and a pointer into it would already be stale.
constexpr const char* kDragPayload = "ed.outliner.entity";

void beginDragSource(const OutlinerNode& node, const OutlinerActions& actions)
{
    if (!actions.reparent)
        return;
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover))
        return;
    ImGui::SetDragDropPayload(kDragPayload, node.id.c_str(),
                              node.id.size() + 1);
    ImGui::Text("parent %s to...", node.label.c_str());
    ImGui::EndDragDropSource();
}

// Accepts a row as a drop target. `parent` is what the dragged entity becomes a
// child of; empty detaches it.
void acceptDrop(const game::content::AuthorId& parent,
                const OutlinerActions& actions)
{
    if (!actions.reparent)
        return;
    if (!ImGui::BeginDragDropTarget())
        return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragPayload)) {
        // The payload is the id plus its terminator; trusting Data as a string
        // is safe because this is the only thing that ever sets it.
        actions.reparent(std::string(static_cast<const char*>(payload->Data)),
                         parent);
    }
    ImGui::EndDragDropTarget();
}

// Does this subtree contain the row we were asked to reveal? Used to force the
// ancestors open before scrolling, because a row inside a collapsed parent
// cannot be scrolled to -- it is not drawn at all.
bool subtreeHas(const OutlinerNode& node, const game::content::AuthorId& id)
{
    if (node.id == id)
        return true;
    for (const OutlinerNode& child : node.children)
        if (subtreeHas(child, id))
            return true;
    return false;
}

// The two row orders a frame needs: the one being built (rows submitted so far)
// and last frame's, which is the only complete one available while this frame
// is still halfway through drawing.
struct RowOrders {
    OutlinerRowOrder& out;
    const OutlinerRowOrder& previous;
};

// One entity row inside a composed object, and everything under it.
void drawComposedNode(const OutlinerNode& node, const OutlinerActions& actions,
                      RowOrders orders)
{
    ImGui::PushID(node.id.c_str());
    orders.out.ids.push_back(node.id);
    const bool selected = actions.isSelected(node.id);

    // AllowOverlap is not cosmetic: the row spans the full width, and the eye
    // and padlock are drawn *after* it at the right-hand end. Without this the
    // row wins the hit test for the whole span, the toggles' InvisibleButtons
    // never see a click, and both switches are simply dead.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_AllowOverlap |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (node.children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    // The row to reveal may be several levels down a chain the author collapsed.
    if (!actions.reveal.empty() && !node.children.empty() &&
        subtreeHas(node, actions.reveal))
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);

    const bool open = ImGui::TreeNodeEx("##node", flags, " ");
    // Sampled before the tag and the label -- see RowInput.
    const RowInput input = sampleRow();
    revealRow(node.id, actions);
    beginDragSource(node, actions);
    acceptDrop(node.id, actions);
    const bool menuOpen = ImGui::BeginPopupContextItem("##nodectx");
    drawKindLabel(node.kind, node.label,
                  actions.isHidden && actions.isHidden(node.id));
    drawComponentBadge(node.components);
    // A click that a switch took is not a click on the row: flipping the eye
    // must not also select, and must not focus the camera on a double press.
    const bool toggled = drawRowToggles({node.id}, node.id.c_str(), actions) ||
                         pointerInToggleColumn(actions);

    if (input.clicked && !toggled)
        applyClick(node.id, input.mode, actions, orders.previous);
    if (input.doubleClicked && !toggled)
        actions.focus();
    if (menuOpen) {
        if (!selected)
            applyClick(node.id, SelectMode::Replace, actions, orders.previous);
        actions.contextMenu();
        ImGui::EndPopup();
    }

    if (open && !node.children.empty()) {
        const float guideTop = ImGui::GetCursorScreenPos().y;
        for (const OutlinerNode& child : node.children)
            drawComposedNode(child, actions, orders);
        drawIndentGuide(guideTop);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace

std::vector<game::content::AuthorId>
OutlinerRowOrder::between(const game::content::AuthorId& a,
                          const game::content::AuthorId& b) const
{
    std::size_t from = ids.size(), to = ids.size();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == a)
            from = i;
        if (ids[i] == b)
            to = i;
    }
    if (from == ids.size() || to == ids.size())
        return {};
    if (from > to)
        std::swap(from, to);
    return std::vector<game::content::AuthorId>(ids.begin() + std::ptrdiff_t(from),
                                                ids.begin() + std::ptrdiff_t(to) + 1);
}

void drawOutlinerRows(const OutlinerTree& tree, bool filterActive,
                      const OutlinerActions& actions)
{
    // A caller with neither range selection nor reveal still needs somewhere to
    // put the order; it just never reads it back.
    static OutlinerRowOrder scratch;
    drawOutlinerRows(tree, filterActive, actions, scratch);
}

void drawOutlinerRows(const OutlinerTree& tree, bool filterActive,
                      const OutlinerActions& actions, OutlinerRowOrder& order)
{
    // Last frame's order is what a click this frame resolves a range against;
    // this frame's replaces it once every row has been submitted.
    const OutlinerRowOrder previous = order;
    order.ids.clear();
    const RowOrders orders{order, previous};

    for (const OutlinerGroup& group : tree.groups) {
        ImGui::PushID(group.key.c_str());
        // A composed object is its own tree: the root row and the chain under
        // it, never collapsed by prefab.
        if (group.composed && !group.nodes.empty()) {
            drawComposedNode(group.nodes.front(), actions, orders);
            ImGui::PopID();
            continue;
        }
        const bool single = group.nodes.size() == 1;
        if (single)
            orders.out.ids.push_back(group.nodes.front().id);
        bool allSelected = true;
        for (const OutlinerNode& node : group.nodes)
            allSelected = allSelected && actions.isSelected(node.id);

        // AllowOverlap for the same reason as the node rows above: the group's
        // own eye and padlock sit inside this row's span.
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_AllowOverlap;
        if (allSelected)
            flags |= ImGuiTreeNodeFlags_Selected;
        // A single entity is a leaf: no arrow, and the row IS the entity.
        if (single)
            flags |=
                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        // A filter that matched a handful is a search result: opening the
        // groups is what the author asked for. So is a reveal that landed on a
        // row inside this group -- it cannot be scrolled to while collapsed,
        // because it is not drawn at all.
        bool revealInside = false;
        if (!actions.reveal.empty())
            for (const OutlinerNode& node : group.nodes)
                revealInside = revealInside || node.id == actions.reveal;
        if (!single && (filterActive || revealInside))
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);

        // The count is drawn in its own right-aligned column below, not glued
        // to the label, so the name is what the clip takes from.
        const std::string header =
            single ? group.nodes.front().label : group.label;
        const bool open = ImGui::TreeNodeEx("##group", flags, " ");
        // Sampled here, before the tag and the label are drawn -- see RowInput.
        const RowInput input = sampleRow();
        if (single)
            revealRow(group.nodes.front().id, actions);
        // A one-entity group IS that entity, so it drags like one. A prefab
        // group of forty walls is not an entity and cannot be a parent.
        if (single) {
            beginDragSource(group.nodes.front(), actions);
            acceptDrop(group.nodes.front().id, actions);
        }
        bool menuOpen = ImGui::BeginPopupContextItem("##groupctx");
        // Reserve the count column so the name clips before it, not through it.
        const float labelRight =
            single ? 0.0f
                   : toggleColumnLeft() -
                         ImGui::CalcTextSize(
                             std::to_string(group.nodes.size()).c_str())
                             .x -
                         12.0f;
        drawKindLabel(group.kind, header,
                      single && actions.isHidden &&
                          actions.isHidden(group.nodes.front().id),
                      labelRight);
        if (!single)
            drawGroupCount(group.nodes.size());
        bool toggled = false;
        if (single) {
            drawComponentBadge(group.nodes.front().components);
            toggled = drawRowToggles({group.nodes.front().id},
                                     group.nodes.front().id.c_str(), actions);
        } else {
            // The group's own switches, acting on every entity under it.
            toggled = drawRowToggles(groupIds(group), group.key.c_str(), actions);
        }
        toggled = toggled || pointerInToggleColumn(actions);

        if (input.clicked && !toggled) {
            // Clicking a group selects everything in it, which is what makes
            // "give all forty pillars a collider" one action. A leaf selects
            // the one entity it stands for.
            if (single)
                applyClick(group.nodes.front().id, input.mode, actions,
                           orders.previous);
            else
                actions.selectGroup(group, input.mode != SelectMode::Replace);
        }
        if (input.doubleClicked && !toggled)
            actions.focus();
        if (menuOpen) {
            // Right-click acts on the row under the cursor, selecting it first
            // so the menu can never act on something else.
            if (!allSelected)
                actions.selectGroup(group, false);
            actions.contextMenu();
            ImGui::EndPopup();
        }

        if (open && !single) {
            const float guideTop = ImGui::GetCursorScreenPos().y;
            for (const OutlinerNode& node : group.nodes) {
                ImGui::PushID(node.id.c_str());
                orders.out.ids.push_back(node.id);
                const bool selected = actions.isSelected(node.id);
                const bool pressed = ImGui::Selectable(
                    "##row", selected,
                    ImGuiSelectableFlags_AllowDoubleClick |
                        ImGuiSelectableFlags_AllowOverlap);
                const SelectMode mode = ImGui::GetIO().KeyShift ? SelectMode::Range
                                        : ImGui::GetIO().KeyCtrl
                                            ? SelectMode::Toggle
                                            : SelectMode::Replace;
                const bool doubleClicked =
                    pressed &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                revealRow(node.id, actions);
                beginDragSource(node, actions);
                acceptDrop(node.id, actions);
                menuOpen = ImGui::BeginPopupContextItem("##ctx");
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextDisabled("   ");
                drawKindLabel(node.kind, node.label,
                              actions.isHidden && actions.isHidden(node.id));
                drawComponentBadge(node.components);
                const bool toggled =
                    drawRowToggles({node.id}, node.id.c_str(), actions) ||
                    pointerInToggleColumn(actions);

                if (pressed && !toggled)
                    applyClick(node.id, mode, actions, orders.previous);
                if (doubleClicked && !toggled)
                    actions.focus();
                if (menuOpen) {
                    if (!selected)
                        applyClick(node.id, SelectMode::Replace, actions,
                                   orders.previous);
                    actions.contextMenu();
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            drawIndentGuide(guideTop);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (tree.groups.empty())
        ImGui::TextDisabled("nothing matches");

    // The rest of the panel detaches: dropping into empty space is how an
    // entity gets *out* of a hierarchy, and without it the only way back to the
    // world would be to delete and re-place it.
    if (actions.reparent) {
        const ImVec2 remaining = ImGui::GetContentRegionAvail();
        if (remaining.y > 1.0f) {
            ImGui::Dummy(remaining);
            acceptDrop({}, actions);
        }
    }
}

} // namespace ed
