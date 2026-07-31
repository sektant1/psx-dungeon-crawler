#include "ComponentInspector.h"

#include <imgui.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace ed {
namespace {

using game::content::Entity;
using game::content::KitPiece;
using game::content::LightAuthor;

// Every widget goes through this: ImGui reports "being dragged" and "released"
// separately, and the inspector needs both -- the first to keep the viewport in
// step, the second to close the undo entry.
void track(InspectorContext& context)
{
    context.track(ImGui::IsItemEdited(), ImGui::IsItemDeactivatedAfterEdit());
}

// std::string field through a fixed buffer, which is what ImGui wants. 96 is
// wider than any id the scene format holds.
void stringField(const char* label, std::string& value,
                 InspectorContext& context)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (ImGui::InputText(label, buffer, sizeof(buffer)))
        value = buffer;
    track(context);
}

void drawMesh(Entity& entity, InspectorContext& context)
{
    const KitPiece* piece =
        context.catalog ? context.catalog->find(entity.prefab) : nullptr;
    ImGui::Text("prefab  %s", entity.prefab.c_str());
    if (!piece) {
        // Resolver state, visible: a missing piece is an authoring signal, not
        // something to paper over with a default cube.
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "mesh    UNRESOLVED");
        return;
    }
    ImGui::TextDisabled("mesh    %s", piece->meshPath.c_str());
    ImGui::TextDisabled("socket  %s  span %d", socketName(piece->socket),
                        piece->span);

    // Material override. Empty means the kit piece's own, which is what nearly
    // everything should use; the override is for the one-off.
    const std::string current = entity.material.empty()
                                    ? piece->material + "  (from kit)"
                                    : entity.material;
    if (ImGui::BeginCombo("material", current.c_str())) {
        if (ImGui::Selectable("(from kit)", entity.material.empty())) {
            entity.material.clear();
            context.track(true, true);
        }
        if (context.materialNames) {
            for (const std::string& option : *context.materialNames) {
                if (ImGui::Selectable(option.c_str(),
                                      option == entity.material)) {
                    entity.material = option;
                    context.track(true, true);
                }
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Checkbox("cast shadows", &entity.castShadows))
        context.track(true, true);
}

void drawCell(Entity& entity, InspectorContext& context)
{
    ImGui::Text("cell %d,%d  edge %d  span %d", entity.cell->col,
                entity.cell->row, int(entity.cell->edge), entity.cell->span);
    ImGui::DragInt("yaw quarters", &entity.cell->yawQuarters, 0.1f, 0, 3);
    track(context);
    ImGui::DragFloat("level", &entity.cell->level, 0.05f);
    track(context);
}

void drawCollider(Entity& entity, InspectorContext& context)
{
    ImGui::DragFloat3("half extents", &entity.collider->halfExtents.x, 0.05f,
                      0.0f, 100.0f);
    track(context);
    ImGui::DragFloat3("offset", &entity.collider->offset.x, 0.05f);
    track(context);
}

void drawLight(Entity& entity, InspectorContext& context)
{
    int type = entity.light->type == LightAuthor::Type::Directional ? 0 : 1;
    if (ImGui::Combo("type", &type, "directional\0point\0"))
        entity.light->type = type == 0 ? LightAuthor::Type::Directional
                                       : LightAuthor::Type::Point;
    track(context);
    ImGui::ColorEdit3("colour", &entity.light->colour.x,
                      ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    track(context);
    if (entity.light->type == LightAuthor::Type::Point) {
        ImGui::DragFloat("range", &entity.light->range, 0.25f, 0.0f, 200.0f);
        track(context);
    }
    if (ImGui::Checkbox("light casts shadows", &entity.light->castShadows))
        context.track(true, true);
}

void drawPlayerSpawn(Entity&, InspectorContext&)
{
    ImGui::TextDisabled("the player starts here, facing the entity's yaw");
}

void drawExit(Entity& entity, InspectorContext& context)
{
    ImGui::DragFloat("exit yaw", &*entity.exitYawDegrees, 1.0f);
    track(context);
}

void drawMarker(Entity& entity, InspectorContext& context)
{
    stringField("marker", *entity.marker, context);
}

void drawEnemySpawn(Entity& entity, InspectorContext& context)
{
    stringField("enemy", *entity.enemySpawn, context);
}

void drawPickup(Entity& entity, InspectorContext& context)
{
    stringField("pickup", *entity.pickup, context);
}

void drawTrigger(Entity& entity, InspectorContext& context)
{
    ImGui::DragFloat3("size", &entity.trigger->size.x, 0.05f, 0.0f, 50.0f);
    track(context);
    stringField("event", entity.trigger->event, context);
}

struct Drawer {
    const char* id; // ComponentType::id
    void (*draw)(Entity& entity, InspectorContext& context);
};

constexpr Drawer kDrawers[] = {
    {"mesh", drawMesh},
    {"cell", drawCell},
    {"collider", drawCollider},
    {"light", drawLight},
    {"player_spawn", drawPlayerSpawn},
    {"exit", drawExit},
    {"marker", drawMarker},
    {"enemy_spawn", drawEnemySpawn},
    {"pickup", drawPickup},
    {"trigger", drawTrigger},
};

} // namespace

void drawEntityIdentity(Entity& entity, InspectorContext& context)
{
    ImGui::Text("id      %s", entity.id.c_str());
    stringField("name", entity.name, context);

    ImGui::SeparatorText("transform");
    ImGui::DragFloat3("position", &entity.transform.position.x, 0.05f);
    track(context);
    ImGui::DragFloat3("rotation", &entity.transform.rotationDegrees.x, 1.0f);
    track(context);
    ImGui::DragFloat3("scale", &entity.transform.scale.x, 0.01f, 0.001f,
                      100.0f);
    track(context);
}

void drawComponentBody(const ComponentType& type, Entity& entity,
                       InspectorContext& context)
{
    for (const Drawer& drawer : kDrawers) {
        if (std::string_view(drawer.id) == type.id) {
            drawer.draw(entity, context);
            return;
        }
    }
    // A component in the registry with no drawer here. Says so rather than
    // rendering an empty section, so the gap is obvious the first time it
    // opens.
    ImGui::TextDisabled("no inspector for '%s'", type.id);
}

} // namespace ed
