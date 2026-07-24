#include "InspectorRegistry.h"

#include "GameComponents.h"
#include "MeshSource.h"

#include <eng/ecs/Components.h>

#include <imgui.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <cstdio>

namespace editor {

const InspectorEntry* InspectorRegistry::find(uint16_t id) const
{
    for (const InspectorEntry& e : mEntries)
        if (e.stableTypeId == id) return &e;
    return nullptr;
}

namespace {

bool drawName(entt::registry& r, entt::entity e)
{
    auto& n = r.get<eng::ecs::Name>(e);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", n.value.c_str());
    if (ImGui::InputText("Name", buf, sizeof(buf))) n.value = buf;
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool drawTransform(entt::registry& r, entt::entity e)
{
    auto& t = r.get<eng::ecs::Transform>(e);
    bool changed = false;
    changed |= ImGui::DragFloat3("Position", glm::value_ptr(t.position), 0.05f);
    changed |= ImGui::DragFloat3("Scale", glm::value_ptr(t.scale), 0.05f);
    glm::vec3 euler = glm::degrees(glm::eulerAngles(t.rotation));
    if (ImGui::DragFloat3("Rotation", glm::value_ptr(euler), 0.5f)) {
        t.rotation = glm::quat(glm::radians(euler));
        changed = true;
    }
    return changed;
}

bool drawMesh(entt::registry& r, entt::entity e)
{
    auto& m = r.get<eng::ecs::MeshRenderer>(e);
    ImGui::Text("Mesh: %s", r.all_of<mapio::MeshSource>(e)
                                ? r.get<mapio::MeshSource>(e).path.c_str() : "?");
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", m.material.c_str());
    if (ImGui::InputText("Material", buf, sizeof(buf))) m.material = buf;
    bool changed = ImGui::IsItemDeactivatedAfterEdit();
    changed |= ImGui::Checkbox("Cast shadows", &m.castShadows);
    return changed;
}

bool drawLight(entt::registry& r, entt::entity e)
{
    auto& l = r.get<eng::ecs::LightRef>(e).desc;
    bool changed = false;
    int type = int(l.type);
    if (ImGui::Combo("Type", &type, "Directional\0Point\0")) {
        l.type = eng::LightDesc::Type(type); changed = true;
    }
    changed |= ImGui::ColorEdit3("Colour", glm::value_ptr(l.colour));
    changed |= ImGui::DragFloat("Range", &l.range, 0.1f, 0.0f, 200.0f);
    changed |= ImGui::Checkbox("Cast shadows", &l.castShadows);
    return changed;
}

bool drawCollider(entt::registry& r, entt::entity e)
{
    auto& c = r.get<game::Collider>(e);
    return ImGui::DragFloat3("Half extents", glm::value_ptr(c.size), 0.05f);
}

bool drawExit(entt::registry& r, entt::entity e)
{
    auto& x = r.get<game::Exit>(e);
    return ImGui::DragFloat("Yaw (deg)", &x.yawDegrees, 1.0f, -360.0f, 360.0f);
}

bool drawEnemy(entt::registry& r, entt::entity e)
{
    auto& s = r.get<game::EnemySpawn>(e);
    char buf[64]; std::snprintf(buf, sizeof(buf), "%s", s.type.c_str());
    if (ImGui::InputText("Enemy type", buf, sizeof(buf))) s.type = buf;
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool drawPickup(entt::registry& r, entt::entity e)
{
    auto& s = r.get<game::Pickup>(e);
    char buf[64]; std::snprintf(buf, sizeof(buf), "%s", s.type.c_str());
    if (ImGui::InputText("Pickup type", buf, sizeof(buf))) s.type = buf;
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool drawTrigger(entt::registry& r, entt::entity e)
{
    auto& t = r.get<game::Trigger>(e);
    bool changed = false;
    changed |= ImGui::DragFloat3("Half extents", glm::value_ptr(t.size), 0.05f);
    char buf[64]; std::snprintf(buf, sizeof(buf), "%s", t.event.c_str());
    if (ImGui::InputText("Event", buf, sizeof(buf))) t.event = buf;
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    return changed;
}

bool drawTag(entt::registry&, entt::entity) { ImGui::TextDisabled("(no fields)"); return false; }

InspectorRegistry build()
{
    InspectorRegistry reg;
    reg.add({1, "Name", drawName});
    reg.add({2, "Transform", drawTransform});
    reg.add({3, "MeshRenderer", drawMesh});
    reg.add({4, "LightRef", drawLight});
    reg.add({10, "Collider", drawCollider});
    reg.add({11, "PlayerSpawn", drawTag});
    reg.add({12, "Exit", drawExit});
    reg.add({13, "EnemySpawn", drawEnemy});
    reg.add({14, "Pickup", drawPickup});
    reg.add({15, "Trigger", drawTrigger});
    return reg;
}

} // namespace

const InspectorRegistry& inspectorRegistry()
{
    static const InspectorRegistry reg = build();
    return reg;
}

} // namespace editor
