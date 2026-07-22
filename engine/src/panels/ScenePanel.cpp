#include "ScenePanel.h"
#include "Selection.h"
#include <eng/SceneView.h>
#include <imgui.h>

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
    const bool open = ImGui::TreeNodeEx(info.name.empty() ? "(node)" : info.name.c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) sel.node = n;
    if (open) { for (NodeHandle c : kids) drawNode(scene, c, sel); ImGui::TreePop(); }
    ImGui::PopID();
}

} // namespace eng::ui
