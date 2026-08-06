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

    // Tall enough for the toolbar's two wrapped rows *plus the dock node's own
    // tab bar*, which is the part the old 64px forgot: the tab took roughly a
    // third of the node and the controls were drawn into what was left, so the
    // bottom of every button sat under the panel below it.
    //
    // Rows are sized from the ImGui defaults the editor runs with (a ~23px
    // frame at 1.0 scale) rather than measured, because the workspace is built
    // before the first frame draws anything.
    constexpr float kToolbarRowPixels = 26.0f;
    constexpr float kToolbarRows = 2.0f;
    constexpr float kDockTabBarPixels = 26.0f;
    constexpr float kToolbarPaddingPixels = 14.0f;
    plan.commandBarPixels =
        (kToolbarRowPixels * kToolbarRows + kDockTabBarPixels +
         kToolbarPaddingPixels) *
        std::clamp(uiScale, 0.80f, 1.50f);
    // ...but never at the workspace's expense. On a short window at 2x text the
    // requested height is a fifth of the screen; the split clamps it anyway, so
    // clamping here as well is what keeps the plan honest about what it will
    // get. The toolbar wraps to fewer visible rows rather than growing.
    plan.commandBarPixels = std::min(plan.commandBarPixels, height * 0.16f);
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

    // Reference flow: the left rail is one node the author tabs between, scene
    // in centre, selected object on right, diagnostics below scene.
    //
    // Asset Browser and Hierarchy were stacked, which split the rail's height
    // between two lists that are each read top-to-bottom -- so both were short,
    // and a deep scene meant scrolling a third of a panel while the catalogue
    // sat half empty above it. They answer different questions ("what can I
    // place" versus "what is already here") and are never read at once, which
    // is what a tab is for.
    ImGui::DockBuilderDockWindow(workspace_window::kAssetBrowser, left);
    ImGui::DockBuilderDockWindow(workspace_window::kHierarchy, left);
    ImGui::DockBuilderDockWindow(workspace_window::kLayers, left);
    ImGui::DockBuilderDockWindow(workspace_window::kInspector, right);
    ImGui::DockBuilderDockWindow(workspace_window::kCommandBar, commandBar);
    ImGui::DockBuilderDockWindow(workspace_window::kHudPreview, centre);
    ImGui::DockBuilderDockWindow(workspace_window::kSceneView, centre);
    ImGui::DockBuilderDockWindow(workspace_window::kConsole, diagnostics);
    ImGui::DockBuilderDockWindow(workspace_window::kProblems, diagnostics);
    ImGui::DockBuilderFinish(dock);
}

} // namespace ed
