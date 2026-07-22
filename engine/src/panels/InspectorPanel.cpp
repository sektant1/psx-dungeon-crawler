#include "InspectorPanel.h"
#include "ScenePanel.h"
#include "Selection.h"
#include <eng/Renderer.h>
#include <eng/SceneView.h>
#include <imgui.h>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace eng::ui {

namespace { // local sRGB<->linear helpers (colour widgets edit sRGB; engine stores linear)
glm::vec3 toSrgb(glm::vec3 l){ return glm::pow(glm::max(l,glm::vec3(0)), glm::vec3(1/2.2f)); }
glm::vec3 toLinear(glm::vec3 s){ return glm::pow(glm::max(s,glm::vec3(0)), glm::vec3(2.2f)); }
}

void InspectorPanel::draw(Renderer& r, const SceneView& scene, Selection& sel) {
    static int addCounter = 0;
    if (ImGui::Button("Add Entity")) {
        const std::string name = "Entity " + std::to_string(++addCounter);
        sel.node = r.createNode(kRootNode, glm::vec3(0.0f), name);
    }
    ImGui::Separator();
    NodeInfo info;
    if (sel.has() && scene.info(sel.node, info)) {
        ImGui::Text("%s", info.name.c_str());
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 pos = info.position;
            if (ImGui::DragFloat3("Position", &pos.x, 0.05f)) r.setPosition(sel.node, pos);
            glm::vec3 deg = glm::degrees(glm::eulerAngles(info.orientation));
            if (ImGui::DragFloat3("Rotation", &deg.x, 0.5f))
                r.setOrientation(sel.node, glm::quat(glm::radians(deg)));
            glm::vec3 scl = info.scale;
            if (ImGui::DragFloat3("Scale", &scl.x, 0.01f, 0.001f, 1000.0f)) {
                scl = glm::max(scl, glm::vec3(0.001f));
                r.setScale(sel.node, scl);
            }
            bool vis = info.visible;
            if (ImGui::Checkbox("Visible", &vis)) r.setNodeVisible(sel.node, vis);
        }
        for (const AttachmentInfo& a : info.attachments) {
            ImGui::PushID(int(a.handle) ^ int(a.kind));
            if (a.kind == NodeAttachKind::Mesh) {
                if (ImGui::CollapsingHeader("Mesh Component", ImGuiTreeNodeFlags_DefaultOpen))
                    ImGui::Text("Material: %s", a.label.c_str());
            } else if (a.kind == NodeAttachKind::Light) {
                if (ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen)) {
                    LightDesc ld;
                    if (scene.lightInfo(LightHandle{uint32_t(a.handle)}, ld)) {
                        glm::vec3 c = toSrgb(ld.colour);
                        if (ImGui::ColorEdit3("Diffuse", &c.x))
                            r.setLightColour(LightHandle{uint32_t(a.handle)}, toLinear(c));
                        float range = ld.range;
                        if (ImGui::DragFloat("Range", &range, 0.1f, 0.1f, 200.0f))
                            r.setLightRange(LightHandle{uint32_t(a.handle)}, range);
                    }
                }
            } else if (a.kind == NodeAttachKind::Sprite) {
                if (ImGui::CollapsingHeader("Sprite Component"))
                    ImGui::TextDisabled("sprite");
            } else {
                if (ImGui::CollapsingHeader("Particles Component"))
                    ImGui::Text("Effect: %s", a.label.c_str());
            }
            ImGui::PopID();
        }
    } else {
        ImGui::TextDisabled("No selection");
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Entities");
    mTree.drawList(scene, sel);
}

} // namespace eng::ui
