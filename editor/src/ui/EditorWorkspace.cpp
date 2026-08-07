#include <editor/ui/EditorWorkspace.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace ed {

WorkspacePlan makeWorkspacePlan(float width, float height, float uiScale)
{
    width = std::max(width, 1.0f);
    height = std::max(height, 1.0f);

    // Large text should enlarge controls, but letting a 2x preference double
    // both rails would erase the workspace. Rails stop growing at 1.25x and
    // scroll their content instead.
    const float density = std::clamp(uiScale, 0.80f, 1.25f);

    WorkspacePlan plan;
    plan.leftPixels = std::clamp(width * 0.19f, 248.0f, 320.0f) * density;
    plan.rightPixels = std::clamp(width * 0.225f, 296.0f, 440.0f) * density;

    // Protect enough central width for scene authoring at high UI scales and
    // small desktop resolutions. Both rails compress proportionally.
    const float minimumCentre = std::min(width * 0.65f, 600.0f * density);
    const float railBudget = std::max(width - minimumCentre, 1.0f);
    const float requestedRails = plan.leftPixels + plan.rightPixels;
    if (requestedRails > railBudget) {
        const float squeeze = railBudget / requestedRails;
        plan.leftPixels *= squeeze;
        plan.rightPixels *= squeeze;
    }

    // The bottom panel opens at roughly a quarter of the window and is dragged
    // from there. It is not a dock split any more, so this is a starting height
    // rather than a reservation -- when it is closed, and it starts closed, the
    // workspace has all of the height.
    plan.bottomPanelPixels =
        std::clamp(height * 0.24f, 160.0f * density, 320.0f * density);

    // The scene tree gets the larger share. A level's hierarchy is the thing an
    // author is navigating continuously; the file system is dipped into, and
    // has a search box for the times it is not.
    //
    // The share tips toward the tree on tall windows and away on short ones,
    // because the file list has a preview swatch and a metadata block above it
    // that do not shrink -- at 600px a 60/40 split leaves it showing two rows.
    plan.sceneTreeFraction = std::clamp(0.35f + height / 5000.0f, 0.42f, 0.62f);
    return plan;
}

void buildEditorWorkspace(std::uint32_t dockspaceId, float width, float height,
                          float uiScale)
{
    const ImGuiID dock = ImGuiID(dockspaceId);
    const WorkspacePlan plan = makeWorkspacePlan(width, height, uiScale);

    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImVec2(width, height));

    ImGuiID centre = dock;
    ImGuiID left = 0;
    ImGuiID right = 0;
    ImGui::DockBuilderSplitNode(
        centre, ImGuiDir_Left,
        std::clamp(plan.leftPixels / std::max(width, 1.0f), 0.10f, 0.36f),
        &left, &centre);

    const float widthAfterLeft = std::max(width - plan.leftPixels, 1.0f);
    ImGui::DockBuilderSplitNode(
        centre, ImGuiDir_Right,
        std::clamp(plan.rightPixels / widthAfterLeft, 0.14f, 0.42f), &right,
        &centre);

    // The left column is two stacked nodes, not one node with tabs.
    ImGuiID leftTop = 0;
    ImGuiID leftBottom = 0;
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Up,
                                std::clamp(plan.sceneTreeFraction, 0.30f, 0.75f),
                                &leftTop, &leftBottom);

    // Layers rides with the tree rather than taking a third node: both answer
    // "what is in this level and can I see it", and a layer list is a handful
    // of rows that would waste a permanent panel.
    ImGui::DockBuilderDockWindow(workspace_window::kSceneTree, leftTop);
    ImGui::DockBuilderDockWindow(workspace_window::kLayers, leftTop);
    ImGui::DockBuilderDockWindow(workspace_window::kFileSystem, leftBottom);

    // Inspector first: it is what a click on anything fills, so it is the tab
    // that must be in front on a fresh workspace.
    ImGui::DockBuilderDockWindow(workspace_window::kInspector, right);
    ImGui::DockBuilderDockWindow(workspace_window::kContract, right);
    ImGui::DockBuilderDockWindow(workspace_window::kHistory, right);

    // 2D docked first so 3D ends up the selected tab: a level is what nearly
    // every scene is, and the switcher raises the other one in one click.
    ImGui::DockBuilderDockWindow(workspace_window::kViewport2D, centre);
    ImGui::DockBuilderDockWindow(workspace_window::kViewport3D, centre);

    // The centre node keeps no tab bar of its own. The main-screen switcher in
    // the top bar is what changes it, and a second control for the same state
    // is how the two end up disagreeing about which one is showing.
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(centre))
        node->LocalFlags |= ImGuiDockNodeFlags_HiddenTabBar;

    ImGui::DockBuilderFinish(dock);
}

} // namespace ed
