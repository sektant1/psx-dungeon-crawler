#include <editor/ui/EditorUi.h>

#include <eng/render/ImGuiHint.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <string_view>

namespace ed::ui {
namespace {

// Stretch-proportional rather than a fixed pixel width, so the same grid reads
// in a 240-pixel dock and a 700-pixel one. A fixed label column is what makes a
// narrow inspector crush its widgets into fifty pixels.
constexpr ImGuiTableFlags kGridFlags = ImGuiTableFlags_SizingStretchProp |
                                       ImGuiTableFlags_NoSavedSettings |
                                       ImGuiTableFlags_PadOuterX;

} // namespace

PropertyGrid::PropertyGrid(const char* id, float labelFraction)
{
    const float fraction = std::clamp(labelFraction, 0.2f, 0.6f);
    mOpen = ImGui::BeginTable(id, 2, kGridFlags);
    if (!mOpen)
        return;
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch,
                            fraction);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch,
                            1.0f - fraction);
}

PropertyGrid::~PropertyGrid()
{
    if (mOpen)
        ImGui::EndTable();
}

void PropertyGrid::row(const char* label, const char* units, const char* hintId,
                       const char* hintFallback)
{
    if (!mOpen)
        return;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    // Aligned to the frame padding so the name sits on the widget's centre
    // line rather than its top edge -- the single thing that makes a column of
    // rows read as a table instead of as text with boxes next to it.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (units && *units) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextDisabled("(%s)", units);
    }
    if (hintId && *hintId)
        eng::imguihint::hover(hintId, hintFallback);
    ImGui::TableNextColumn();
    // The widget fills its cell. Callers pass "##name" labels; the name is
    // already on screen and ImGui would otherwise draw it twice.
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void PropertyGrid::full(const char* text)
{
    if (!mOpen)
        return;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", text);
}

AssetPreviewLayout assetPreviewLayout(float availableWidth)
{
    AssetPreviewLayout layout;
    // Between a third and a half of the panel, bounded: below ~80 pixels a
    // material swatch stops showing its texture, above ~144 it starts eating
    // the list it is supposed to be helping you read.
    layout.thumbnailSize = std::clamp(availableWidth * 0.36f, 80.0f, 144.0f);
    layout.metadataBeside = availableWidth >= 300.0f;
    return layout;
}

bool filterMatches(std::string_view text, std::string_view query)
{
    if (query.empty())
        return true;
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
                       [](char a, char b) {
                           return std::tolower(static_cast<unsigned char>(a)) ==
                                  std::tolower(static_cast<unsigned char>(b));
                       }) != text.end();
}

std::string humanBytes(unsigned long long bytes)
{
    static const char* kUnits[] = {"B", "KB", "MB", "GB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), unit == 0 ? "%.0f %s" : "%.1f %s",
                  value, kUnits[unit]);
    return buffer;
}

void drawAssetPanel(const AssetPanelView& view)
{
    const AssetPreviewLayout layout =
        assetPreviewLayout(ImGui::GetContentRegionAvail().x);

    // --- preview -----------------------------------------------------------
    if (view.previewTexture != 0) {
        ImGui::Image(static_cast<ImTextureID>(view.previewTexture),
                     ImVec2(layout.thumbnailSize, layout.thumbnailSize));
        if (view.previewTooltip)
            ImGui::SetItemTooltip("%s", view.previewTooltip);
        if (view.onPreviewDrag && ImGui::IsItemActive() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            view.onPreviewDrag(ImGui::GetIO().MouseDelta.x);
        if (layout.metadataBeside)
            ImGui::SameLine();
    }

    // --- metadata and actions ----------------------------------------------
    ImGui::BeginGroup();
    if (view.metadata)
        view.metadata();
    if (view.actions) {
        ImGui::Spacing();
        view.actions();
    }
    ImGui::EndGroup();

    // --- toggles -----------------------------------------------------------
    if (view.toggles) {
        ImGui::Separator();
        view.toggles();
    }

    // --- search ------------------------------------------------------------
    ImGui::Separator();
    if (view.filter && view.filterCapacity > 0) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##assetfilter", view.filterHint, view.filter,
                                 view.filterCapacity);
    }

    // --- list --------------------------------------------------------------
    // Reserves the footer's line so the list ends above it rather than pushing
    // it off the bottom of a short panel.
    const float footerHeight =
        view.footer.empty()
            ? 0.0f
            : ImGui::GetTextLineHeightWithSpacing() +
                  ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::BeginChild("##assetlist", ImVec2(0.0f, -footerHeight))) {
        if (view.list)
            view.list();
    }
    ImGui::EndChild();
    if (!view.footer.empty())
        ImGui::TextDisabled("%s", view.footer.c_str());
}

} // namespace ed::ui
