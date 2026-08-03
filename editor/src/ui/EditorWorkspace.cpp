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

    plan.commandBarPixels = 64.0f * std::clamp(uiScale, 0.80f, 1.50f);
    plan.diagnosticsPixels =
        std::clamp(height * 0.18f, 132.0f * density, 192.0f * density);
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

    ImGuiID commandBar = 0;
    ImGui::DockBuilderSplitNode(
        centre, ImGuiDir_Up,
        std::clamp(plan.commandBarPixels / std::max(height, 1.0f), 0.05f,
                   0.16f),
        &commandBar, &centre);

    const float heightAfterCommand =
        std::max(height - plan.commandBarPixels, 1.0f);
    ImGuiID diagnostics = 0;
    ImGui::DockBuilderSplitNode(
        centre, ImGuiDir_Down,
        std::clamp(plan.diagnosticsPixels / heightAfterCommand, 0.14f, 0.36f),
        &diagnostics, &centre);

    ImGuiID assets = 0;
    ImGuiID hierarchy = left;
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, plan.assetBrowserFraction,
                                &assets, &hierarchy);

    // Reference flow: one asset browser above hierarchy, scene in centre,
    // selected object on right, diagnostics below scene. Asset modes are
    // internal tabs, so they cannot fragment dock topology again.
    ImGui::DockBuilderDockWindow(workspace_window::kAssetBrowser, assets);
    ImGui::DockBuilderDockWindow(workspace_window::kHierarchy, hierarchy);
    ImGui::DockBuilderDockWindow(workspace_window::kInspector, right);
    ImGui::DockBuilderDockWindow(workspace_window::kCommandBar, commandBar);
    ImGui::DockBuilderDockWindow(workspace_window::kHudPreview, centre);
    ImGui::DockBuilderDockWindow(workspace_window::kSceneView, centre);
    ImGui::DockBuilderDockWindow(workspace_window::kConsole, diagnostics);
    ImGui::DockBuilderDockWindow(workspace_window::kProblems, diagnostics);
    ImGui::DockBuilderFinish(dock);
}

} // namespace ed
