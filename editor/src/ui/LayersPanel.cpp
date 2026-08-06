#include <editor/ui/LayersPanel.h>

#include <editor/ui/EditorIcons.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace ed {
namespace {

// The switch column, mirroring OutlinerPanel's: names are the column being
// read, so the per-row switches sit at the right-hand end rather than in front
// of every line.
//
// Three switches here rather than two -- solo joins the eye and the padlock --
// because solo is the one an author reaches for most in a dense level and
// burying it in a context menu would make it the one nobody finds.
//
// Derived from the window rather than from the cursor. A cursor-relative
// version was wrong in a way that is invisible until you use it: the row asks
// for this boundary twice -- once to decide whether a click belonged to the
// switches, and once to place them -- and by the second call the cursor has
// moved along the row, so the two answers differed and clicks near the switches
// were attributed to the wrong thing.
float toggleColumnLeft()
{
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float iconSpan = ImGui::GetFontSize() * 3.0f + 16.0f;
    return right - iconSpan;
}

unsigned packed(const glm::vec3& colour, float alpha)
{
    const auto channel = [](float v) {
        return unsigned(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return channel(alpha) << 24 | channel(colour.z) << 16 |
           channel(colour.y) << 8 | channel(colour.x);
}

// The colour chip at the head of the row. Small and square: it is an
// identification aid, and a swatch big enough to judge a colour by would make
// every row twice as tall for information nobody is reading.
void drawColourChip(const glm::vec3& colour)
{
    const float size = ImGui::GetFontSize() * 0.7f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float pad = (ImGui::GetTextLineHeight() - size) * 0.5f;
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(ImVec2(at.x, at.y + pad),
                        ImVec2(at.x + size, at.y + pad + size),
                        packed(colour, 1.0f));
    ImGui::Dummy(ImVec2(size + 6.0f, ImGui::GetTextLineHeight()));
}

} // namespace

void drawLayerRows(const std::vector<layers::LayerStat>& rows,
                   const LayerActions& actions)
{
    for (const layers::LayerStat& row : rows) {
        // The default layer's id is empty, which is not a usable ImGui id --
        // and two rows pushing "" would collide with each other.
        ImGui::PushID(row.id.empty() ? "##default" : row.id.c_str());

        const bool active = actions.isActive && actions.isActive(row.id);
        const bool hidden = actions.isHidden && actions.isHidden(row.id);
        const bool locked = actions.isLocked && actions.isLocked(row.id);
        const bool solo = actions.isSolo && actions.isSolo(row.id);

        // The row itself: clicking it makes the layer active, which is where
        // the next placed entity lands.
        //
        // Captured before the Selectable, because everything below is drawn
        // back over it and needs the row's own left edge -- taking it from the
        // window would ignore horizontal scroll.
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        const float rowTop = rowStart.y;
        ImGui::Selectable("##row", active,
                          ImGuiSelectableFlags_AllowOverlap);
        const bool rowClicked =
            ImGui::IsItemClicked() &&
            ImGui::GetMousePos().x < toggleColumnLeft();
        if (rowClicked && actions.setActive)
            actions.setActive(row.id);

        if (ImGui::BeginPopupContextItem("##menu")) {
            if (actions.selectMembers &&
                ImGui::MenuItem("Select members", nullptr, false,
                                row.entities > 0))
                actions.selectMembers(row.id);
            if (actions.assignSelection &&
                ImGui::MenuItem("Move selection here", nullptr, false,
                                actions.selectionCount > 0))
                actions.assignSelection(row.id);
            ImGui::Separator();
            if (actions.exportLayer &&
                ImGui::MenuItem("Export layer…", nullptr, false,
                                row.entities > 0))
                actions.exportLayer(row.id);
            if (actions.importLayer &&
                ImGui::MenuItem("Import into layer…"))
                actions.importLayer(row.id);
            // Rename and recolour are only offered on a declared layer: the
            // default layer is implicit and has nothing to write them to.
            if (!row.id.empty() && !row.undeclared) {
                ImGui::Separator();
                if (actions.rename) {
                    char buffer[64] = {};
                    std::snprintf(buffer, sizeof(buffer), "%s",
                                  row.name.c_str());
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::InputText("name", buffer, sizeof(buffer),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                        actions.rename(row.id, buffer);
                }
                if (actions.recolour) {
                    glm::vec3 colour = row.colour;
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::ColorEdit3("colour", &colour.x,
                                          ImGuiColorEditFlags_NoInputs))
                        actions.recolour(row.id, colour);
                }
                ImGui::Separator();
                if (actions.removeLayer &&
                    ImGui::MenuItem("Delete layer")) {
                    // The entities survive; see LayerActions::removeLayer.
                    actions.removeLayer(row.id);
                }
            }
            ImGui::EndPopup();
        }

        // Everything below is drawn over the Selectable, which is why it takes
        // AllowOverlap above.
        ImGui::SetCursorScreenPos(rowStart);
        drawColourChip(row.colour);
        ImGui::SameLine(0.0f, 0.0f);

        // A hidden layer's name is dimmed: the eye says so too, but a list of
        // twelve rows is scanned by its names, not by its icons.
        if (hidden)
            ImGui::TextDisabled("%s", row.name.c_str());
        else if (active)
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),
                               "%s", row.name.c_str());
        else
            ImGui::TextUnformatted(row.name.c_str());

        ImGui::SameLine(0.0f, 8.0f);
        if (row.undeclared) {
            // Not a colour-only signal: the row says what is wrong in words,
            // because "this layer is not declared" is not a thing an author can
            // infer from a shade of amber.
            ImGui::TextDisabled("(undeclared)");
            ImGui::SameLine(0.0f, 8.0f);
        }
        ImGui::TextDisabled("%zu", row.entities);

        // The switches, right-aligned.
        const float size = ImGui::GetFontSize();
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(toggleColumnLeft(), rowTop));
        if (actions.isHidden && actions.setHidden) {
            if (iconToggle(hidden ? Icon::EyeClosed : Icon::Eye, "##vis",
                           !hidden,
                           hidden ? "hidden -- click to show this layer"
                                  : "visible -- click to hide this layer",
                           size))
                actions.setHidden(row.id, !hidden);
            ImGui::SameLine(0.0f, 4.0f);
        }
        if (actions.isLocked && actions.setLocked) {
            if (iconToggle(locked ? Icon::Lock : Icon::Unlock, "##lock", locked,
                           locked ? "locked -- its entities cannot be picked"
                                  : "unlocked -- click to stop picking these",
                           size))
                actions.setLocked(row.id, !locked);
            ImGui::SameLine(0.0f, 4.0f);
        }
        if (actions.isSolo && actions.toggleSolo) {
            if (iconToggle(Icon::Select, "##solo", solo,
                           solo ? "soloed -- click to show the other layers"
                                : "click to show only this layer",
                           size))
                actions.toggleSolo(row.id);
        }

        ImGui::PopID();
    }

    if (actions.addLayer) {
        ImGui::Separator();
        if (ImGui::SmallButton("New layer"))
            actions.addLayer();
        if (actions.assignSelection && actions.selectionCount > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%zu selected -- right-click a layer to move it",
                                actions.selectionCount);
        }
    }
}

} // namespace ed
