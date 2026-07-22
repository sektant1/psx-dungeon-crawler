#include <eng/EditorUi.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <eng/Renderer.h>
#include <eng/SceneView.h>

#include "panels/RenderTuningPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/Selection.h"

namespace eng {

struct EditorUi::Impl {
    explicit Impl(Renderer& renderer) : r(renderer) { tuning.init(&r); }

    Renderer& r;
    eng::ui::RenderTuningPanel tuning;
    eng::ui::InspectorPanel inspector;
    eng::ui::Selection sel;
    Size vpSize;
    bool vpHovered = false;
    bool builtLayout = false;
};

EditorUi::EditorUi(Renderer& r) : mImpl(std::make_unique<Impl>(r)) {}
EditorUi::~EditorUi() = default;

void EditorUi::draw(uint64_t texId)
{
    Impl& s = *mImpl;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags host = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;
    ImGui::Begin("##EditorHost", nullptr, host);
    ImGui::PopStyleVar(3);
    ImGuiID dockId = ImGui::GetID("EditorDockSpace");
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    // Build the default split only once the host size is real. On the very first
    // frame the SDL window size may not have propagated to io.DisplaySize yet, so
    // vp->WorkSize can be tiny -- building then bakes in degenerate ~12px panels.
    if (!s.builtLayout && vp->WorkSize.x > 200.0f && vp->WorkSize.y > 200.0f) {
        s.builtLayout = true;
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);
        ImGuiID center = dockId, left, right, leftBottom;
        left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
        right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);
        leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.35f, nullptr, &left);
        ImGui::DockBuilderDockWindow("Settings", left);
        ImGui::DockBuilderDockWindow("Profiler", leftBottom);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Scene", center);
        ImGui::DockBuilderFinish(dockId);
    }
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("(panels are dockable)", nullptr, false, false);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    ImGui::End(); // host

    ImGui::Begin("Settings");
    s.tuning.drawShaders();
    s.tuning.drawPixelArt();
    s.tuning.drawCamera();
    if (ImGui::Button("Copy all as TOML")) s.tuning.copyToml();
    ImGui::End();

    ImGui::Begin("Profiler");
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
    ImGui::End();

    ImGui::Begin("Inspector");
    s.inspector.draw(s.r, s.r.scene(), s.sel);
    ImGui::End();

    // Explicit RTT viewport. If the GL texture id cannot be resolved, show a
    // visible placeholder so editor/rendering failures are diagnosable.
    ImGui::Begin("Scene");
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    s.vpSize.w = int(avail.x);
    s.vpSize.h = int(avail.y);
    if (texId != 0 && avail.x > 0 && avail.y > 0)
        ImGui::Image((ImTextureID)texId, avail, ImVec2(0, 1), ImVec2(1, 0)); // GL V-flip
    else
        ImGui::TextDisabled("Scene render target unavailable");
    s.vpHovered = ImGui::IsWindowHovered();
    ImGui::End();
}

NodeHandle EditorUi::selected() const { return mImpl->sel.node; }
bool EditorUi::viewportHovered() const { return mImpl->vpHovered; }
EditorUi::Size EditorUi::viewportSize() const { return mImpl->vpSize; }

} // namespace eng
