#include "ScenePanel.h"
#include "Selection.h"
#include <eng/SceneView.h>
#include <imgui.h>
#include <string>

namespace eng::ui {

void ScenePanel::drawList(const SceneView& scene, Selection& sel) {
    for (NodeHandle n : scene.roots()) drawNode(scene, n, sel);
}

void ScenePanel::drawNode(const SceneView& scene, NodeHandle n, Selection& sel) {
    NodeInfo info; if (!scene.info(n, info)) return;
    ImGui::PushID(int(n.id));
    auto kids = scene.childrenOf(n);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (kids.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (sel.node.id == n.id) flags |= ImGuiTreeNodeFlags_Selected;
    if (!info.parent.valid()) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    const bool open = ImGui::TreeNodeEx(info.name.empty() ? "(node)" : info.name.c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) sel.node = n;
    if (open) {
        for (const AttachmentInfo& a : info.attachments) {
            const char* kind = a.kind == NodeAttachKind::Mesh ? "Mesh"
                : a.kind == NodeAttachKind::Light ? "Light"
                : a.kind == NodeAttachKind::Sprite ? "Sprite" : "Particles";
            ImGui::BulletText("%s%s%s", kind, a.label.empty() ? "" : ": ",
                              a.label.c_str());
        }
        for (NodeHandle c : kids) drawNode(scene, c, sel);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace eng::ui
