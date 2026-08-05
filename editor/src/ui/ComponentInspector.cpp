#include <editor/ui/ComponentInspector.h>

#include <eng/render/ImGuiHint.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace ed {
namespace {

using game::content::CameraAuthor;
using game::content::Entity;
using game::content::KitPiece;
using game::content::LightAuthor;
using game::content::OrbitAuthor;
using game::content::PortalAuthor;
using game::content::SpinAuthor;

// Every widget goes through this: ImGui reports "being dragged" and "released"
// separately, and the inspector needs both -- the first to keep the viewport in
// step, the second to close the undo entry.
void track(InspectorContext& context)
{
    context.track(ImGui::IsItemEdited(), ImGui::IsItemDeactivatedAfterEdit());
}

const ImVec4 kAxisColours[] = {
    ImVec4(0.95f, 0.35f, 0.38f, 1.0f),
    ImVec4(0.48f, 0.88f, 0.40f, 1.0f),
    ImVec4(0.38f, 0.62f, 1.00f, 1.0f),
};

// A vector property is three numeric fields, not one unlabeled slider. The
// explicit axis labels remain readable without colour, while colour matches the
// viewport gizmo. Narrow inspectors stack the axes instead of crushing them.
void drawVec3Property(const char* id, const char* label, const char* units,
                      glm::vec3& value, const glm::vec3& reset, float speed,
                      InspectorContext& context, float min = 0.0f,
                      float max = 0.0f, const char* format = "%.3f")
{
    ImGui::PushID(id);
    if (ImGui::BeginTable("##header", 2,
                          ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("property", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(label);
        if (units && *units) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", units);
        }
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("Reset")) {
            value = reset;
            context.track(true, true);
        }
        eng::imguihint::hover("editor.inspector.reset",
                              "Restore this property to its default value.");
        ImGui::EndTable();
    }

    const bool bounded = max > min;
    const ImGuiSliderFlags flags =
        bounded ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
    const bool horizontal = ImGui::GetContentRegionAvail().x >= 300.0f;
    const int columns = horizontal ? 3 : 1;
    if (ImGui::BeginTable("##axes", columns,
                          ImGuiTableFlags_SizingStretchSame |
                              ImGuiTableFlags_NoSavedSettings)) {
        static const char* kAxes[] = {"X", "Y", "Z"};
        for (int axis = 0; axis < 3; ++axis) {
            ImGui::TableNextColumn();
            ImGui::PushID(axis);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kAxisColours[axis], "%s", kAxes[axis]);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##value", &value[axis], speed, min, max, format,
                             flags);
            track(context);
            eng::imguihint::hover(
                "editor.inspector.number",
                "Drag to scrub. Click, double-click, or press Enter to type; "
                "Tab moves between axes.");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

// std::string through the fixed buffer ImGui wants. 96 is wider than any id the
// scene format holds.
//
// The one place that marshals a std::string into an InputText. It was three --
// stringField, this, and stringRow -- each with its own copy of the buffer, the
// snprintf and the write-back, which is three places to fix when the width or
// the tracking changes and three chances to miss one.
void stringInput(const char* id, std::string& value, InspectorContext& context)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (ImGui::InputText(id, buffer, sizeof(buffer)))
        value = buffer;
    track(context);
}

// A labelled field outside a grid: ImGui draws the name to the right itself.
void stringField(const char* label, std::string& value,
                 InspectorContext& context)
{
    stringInput(label, value, context);
}

// The same inside a property grid: the name is already in the left column, so
// the widget takes a "##" label and fills the right one.
void stringRow(ui::PropertyGrid& grid, const char* label, std::string& value,
               InspectorContext& context)
{
    grid.row(label);
    char id[64];
    std::snprintf(id, sizeof(id), "##%s", label);
    stringInput(id, value, context);
}

// Hovering a row previews its subject in the shared swatch and shows it in the
// tooltip. Selecting commits. That split is what makes scrubbing a long list to
// find the right material or effect actually work, and it is the same rule the
// Material panel's own grid follows.
void previewTooltip(InspectorContext& context,
                    const std::function<void(const std::string&)>& request,
                    const std::string& name, const char* note)
{
    if (!ImGui::IsItemHovered() || !request)
        return;
    request(name);
    if (context.previewTexture == 0 && !note)
        return;
    ImGui::BeginTooltip();
    if (context.previewTexture != 0)
        ImGui::Image(static_cast<ImTextureID>(context.previewTexture),
                     ImVec2(128.0f, 128.0f));
    ImGui::TextUnformatted(name.c_str());
    if (note && *note)
        ImGui::TextWrapped("%s", note);
    ImGui::EndTooltip();
}

// The material combo, shared by all three geometry drawers.
//
// A kit piece has a material to fall back on and the other two do not, which is
// the only difference between them: what a mesh wears is one question, and it
// used to be answered only for prefabs -- so a mesh file or a primitive placed
// in a level had no way to be dressed at all.
void drawMaterialChoice(Entity& entity, InspectorContext& context,
                        const char* emptyLabel)
{
    const std::string current =
        entity.material.empty() ? emptyLabel : entity.material;
    if (ImGui::BeginCombo("##material", current.c_str())) {
        if (ImGui::Selectable(emptyLabel, entity.material.empty())) {
            entity.material.clear();
            context.track(true, true);
        }
        if (context.materials) {
            for (const MaterialInfo& info : *context.materials) {
                if (!isEntityMaterial(info.klass))
                    continue;
                const MaterialAdvice advice =
                    materialFits(info.klass, context.meshKind);
                if (advice.fit != Fit::Good)
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(0.95f, 0.82f, 0.38f, 1.0f));
                if (ImGui::Selectable(info.name.c_str(),
                                      info.name == entity.material)) {
                    entity.material = info.name;
                    context.track(true, true);
                }
                if (advice.fit != Fit::Good)
                    ImGui::PopStyleColor();
                previewTooltip(context, context.requestMaterialPreview,
                               info.name,
                               advice.fit != Fit::Good ? advice.reason.c_str()
                                                       : nullptr);
            }
        } else if (context.materialNames) {
            for (const std::string& option : *context.materialNames) {
                if (ImGui::Selectable(option.c_str(),
                                      option == entity.material)) {
                    entity.material = option;
                    context.track(true, true);
                }
                previewTooltip(context, context.requestMaterialPreview, option,
                               nullptr);
            }
        }
        ImGui::EndCombo();
    }
}

// What the chosen material is doing to this mesh, stated on the panel that set
// it. An override that renders wrongly is otherwise invisible until the level
// is looked at from the right angle.
void drawMaterialAdvice(const Entity& entity, InspectorContext& context)
{
    if (entity.material.empty() || !context.materials)
        return;
    for (const MaterialInfo& info : *context.materials) {
        if (info.name != entity.material)
            continue;
        const MaterialAdvice advice = materialFits(info.klass, context.meshKind);
        if (advice.fit != Fit::Good) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(0.95f, 0.82f, 0.38f, 1.0f));
            ImGui::TextWrapped("%s", advice.reason.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("%s material", materialClassName(info.klass));
        }
        break;
    }
}

// A mesh file named directly. Everything about it is in the entity, which is
// the point: no kit.toml entry, no socket, no grid -- just geometry.
void drawMeshAsset(Entity& entity, InspectorContext& context)
{
    ui::PropertyGrid grid("##mesh_asset");
    grid.row("path");
    // Read-only, and deliberately: a mesh path is chosen from a list of what
    // exists, and a text field here is how a level acquires a reference to a
    // file nobody ever had. The Placeables list is the picker.
    ImGui::TextUnformatted(entity.mesh->path.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\n\nPick a different one in the Asset Browser's "
                          "Placeables tab, then press Apply to Selection.",
                          entity.mesh->path.c_str());

    grid.row("import scale", nullptr, "editor.inspector.import_scale",
             "Multiplies this entity's transform scale. For a file authored in "
             "units other than metres.");
    ImGui::DragFloat("##import_scale", &entity.mesh->importScale, 0.01f, 0.001f,
                     100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    track(context);

    grid.row("material");
    drawMaterialChoice(entity, context, "(renderer default)");

    grid.row("cast shadows");
    if (ImGui::Checkbox("##cast_shadows", &entity.castShadows))
        context.track(true, true);
    drawMaterialAdvice(entity, context);
}

// The generated mesh.
void drawPrimitive(Entity& entity, InspectorContext& context)
{
    ui::PropertyGrid grid("##primitive");
    drawPrimitiveFields(*entity.primitive, grid, &context);

    grid.row("material");
    drawMaterialChoice(entity, context, "(renderer default)");

    grid.row("cast shadows");
    if (ImGui::Checkbox("##cast_shadows", &entity.castShadows))
        context.track(true, true);
    drawMaterialAdvice(entity, context);
}

// The kit piece. Its mesh, socket and material all come from kit.toml, so most
// of this is read-only: what an author decides here is the one-off override.
void drawMesh(Entity& entity, InspectorContext& context)
{
    const KitPiece* piece =
        context.catalog ? context.catalog->find(entity.prefab) : nullptr;
    ui::PropertyGrid grid("##kit_piece");

    grid.row("prefab");
    ImGui::TextUnformatted(entity.prefab.c_str());
    if (!piece) {
        // Resolver state, visible: a missing piece is an authoring signal, not
        // something to paper over with a default cube.
        grid.row("mesh");
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "UNRESOLVED");
        return;
    }
    grid.row("mesh");
    ImGui::TextDisabled("%s", piece->meshPath.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", piece->meshPath.c_str());
    grid.row("socket");
    ImGui::TextDisabled("%s  |  span %d", socketName(piece->socket),
                        piece->span);

    // Material override. Empty means the kit piece's own, which is what nearly
    // everything should use; the override is for the one-off. The combo is the
    // shared one -- what a mesh wears is one question, asked identically of a
    // kit piece, a mesh file and a primitive.
    grid.row("material");
    const std::string fromKit = piece->material + "  (from kit)";
    drawMaterialChoice(entity, context, fromKit.c_str());
    grid.row("cast shadows");
    if (ImGui::Checkbox("##kit_shadows", &entity.castShadows))
        context.track(true, true);
    drawMaterialAdvice(entity, context);
}

void drawCell(Entity& entity, InspectorContext& context)
{
    ui::PropertyGrid grid("##cell");
    grid.row("cell");
    ImGui::Text("%d, %d  |  edge %d  |  span %d", entity.cell->col,
                entity.cell->row, int(entity.cell->edge), entity.cell->span);
    grid.row("yaw", "quarter turns");
    bool placementChanged =
        ImGui::DragInt("##yaw", &entity.cell->yawQuarters, 0.1f, 0, 3, "%d",
                       ImGuiSliderFlags_AlwaysClamp);
    track(context);
    grid.row("level", "m");
    placementChanged = ImGui::DragFloat("##level", &entity.cell->level, 0.05f,
                                        0.0f, 0.0f, "%.2f") ||
                       placementChanged;
    track(context);
    if (placementChanged && context.grid && context.catalog) {
        if (const KitPiece* piece = context.catalog->find(entity.prefab)) {
            const glm::vec3 scale = entity.transform.scale;
            entity.transform = game::content::placementToTransform(
                *context.grid, *context.catalog, *piece, *entity.cell);
            entity.transform.scale = scale;
        }
    }
    grid.full("grid placement drives position and yaw");
}

void drawCollider(Entity& entity, InspectorContext& context)
{
    drawVec3Property("half_extents", "Half Extents", "m",
                     entity.collider->halfExtents, glm::vec3(0.5f), 0.05f,
                     context, 0.001f, 100.0f);
    drawVec3Property("offset", "Offset", "m", entity.collider->offset,
                     glm::vec3(0.0f), 0.05f, context);
}

// A light authored for this renderer: a hue, an energy, and a reach.
//
// The colour a light carries is pre-multiplied by its energy -- a torch is
// authored well above 1.0 so the bloom pass catches it -- which makes a plain
// RGB picker the wrong control. Dragging "brightness" up used to mean opening
// the picker and multiplying three numbers by hand, and the hue drifted every
// time. Hue and energy are separated here and recombined on the way out, which
// is how every engine that has an HDR light exposes one.
void drawLightAnimation(LightAuthor& light, InspectorContext& context,
                        ui::PropertyGrid& grid);

void drawLight(Entity& entity, InspectorContext& context)
{
    LightAuthor& light = *entity.light;
    ui::PropertyGrid grid("##light");
    grid.row("type");
    int type = light.type == LightAuthor::Type::Directional ? 0 : 1;
    if (ImGui::Combo("##type", &type, "directional\0point\0"))
        light.type = type == 0 ? LightAuthor::Type::Directional
                               : LightAuthor::Type::Point;
    track(context);

    // A light stores one over-bright colour; the panel edits it as a hue and a
    // brightness. That split has to be REMEMBERED, not re-derived from the
    // product each frame, because the decomposition is not unique: deriving
    // brightness as the largest channel forces the hue's largest channel to 1,
    // so picking a dim colour silently moved its darkness into the brightness
    // field, which then jumped under the cursor -- and dragging brightness to
    // zero collapsed the colour to black and lost the hue for good, handing
    // back white on the way up.
    //
    // Keyed by entity so switching selection re-derives, and re-derived anyway
    // whenever the stored colour stops matching the split (undo, a preset, or
    // any other writer).
    static game::content::AuthorId sSplitOwner;
    static glm::vec3 sHue{1.0f};
    static float sEnergy = 1.0f;
    const glm::vec3 recomposed = sHue * sEnergy;
    const bool stale = sSplitOwner != entity.id ||
                       std::abs(recomposed.r - light.colour.r) > 1e-4f ||
                       std::abs(recomposed.g - light.colour.g) > 1e-4f ||
                       std::abs(recomposed.b - light.colour.b) > 1e-4f;
    if (stale) {
        sSplitOwner = entity.id;
        sEnergy = std::max({light.colour.r, light.colour.g, light.colour.b});
        sHue = sEnergy > 1e-4f ? light.colour / sEnergy : glm::vec3(1.0f);
    }

    bool recombine = false;
    grid.row("colour");
    if (ImGui::ColorEdit3("##colour", &sHue.x, ImGuiColorEditFlags_Float))
        recombine = true;
    track(context);
    grid.row("brightness");
    if (ImGui::DragFloat("##brightness", &sEnergy, 0.02f, 0.0f, 20.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp))
        recombine = true;
    track(context);
    if (recombine) {
        sEnergy = std::max(sEnergy, 0.0f);
        light.colour = sHue * sEnergy;
    }
    if (sEnergy > 1.0f)
        grid.full("above 1.0 -- feeds the bloom pass");

    // The presets are the values the game's own lights use. A level author
    // reaching for "a torch" should get the torch this game has, not a guess
    // that reads wrong next to the ones the dungeon generator places.
    struct Preset {
        const char* label;
        glm::vec3 colour;
        float range;
    };
    static const Preset kPresets[] = {
        {"torch", {1.00f, 0.75f, 0.45f}, 8.0f},
        {"brazier", {1.60f, 0.90f, 0.40f}, 11.0f},
        {"candle", {1.00f, 0.80f, 0.55f}, 3.5f},
        {"arcane", {0.42f, 1.60f, 2.35f}, 8.5f},
        {"fel", {0.22f, 1.05f, 0.10f}, 8.5f},
        {"moon", {0.55f, 0.65f, 1.00f}, 14.0f},
    };
    grid.row("preset");
    if (ImGui::BeginCombo("##preset", "pick a look")) {
        for (const Preset& preset : kPresets) {
            ImGui::PushID(preset.label);
            if (ImGui::Selectable(preset.label)) {
                light.colour = preset.colour;
                if (light.type == LightAuthor::Type::Point)
                    light.range = preset.range;
                context.track(true, true);
            }
            ImGui::SameLine();
            ImGui::ColorButton(
                "##swatch",
                ImVec4(preset.colour.r, preset.colour.g, preset.colour.b, 1.0f),
                ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f, 14.0f));
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    if (light.type == LightAuthor::Type::Point) {
        grid.row("range", "m");
        ImGui::DragFloat("##range", &light.range, 0.25f, 0.001f, 200.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
        track(context);
        // The kit's cell is 4 m and its rooms are 3 m tall, so a reach stated
        // in cells is the number that answers "does this cross the doorway".
        char reach[64];
        std::snprintf(reach, sizeof(reach), "%.1f cells across",
                      double(light.range * 2.0f / 4.0f));
        grid.full(reach);
    } else {
        grid.full("aimed by the entity's rotation, not placed");
    }
    grid.row("cast shadows");
    if (ImGui::Checkbox("##shadows", &light.castShadows))
        context.track(true, true);
    if (light.castShadows)
        grid.full("stencil shadows, from opted-in casters only");
    drawLightAnimation(light, context, grid);
}

void drawPlayerSpawn(Entity&, InspectorContext&)
{
    ImGui::TextDisabled("the player starts here, facing the entity's yaw");
}

void drawExit(Entity& entity, InspectorContext& context)
{
    ui::PropertyGrid grid("##exit");
    grid.row("yaw", "deg", "editor.inspector.exit_yaw",
             "Which way the player is facing when the next level starts.");
    ImGui::DragFloat("##exit_yaw", &*entity.exitYawDegrees, 1.0f);
    track(context);
}

// The prefixes the runtime actually looks up. A marker whose name matches none
// of them is inert -- valid, saved, cooked, and read by nothing.
void drawMarker(Entity& entity, InspectorContext& context)
{
    {
        ui::PropertyGrid grid("##marker");
        stringRow(grid, "name", *entity.marker, context);
    }

    struct Known {
        const char* prefix;
        const char* what;
    };
    static const Known kKnown[] = {
        {"enemy.", "spawns that enemy id (same as an Enemy Spawn component)"},
        {"feature.", "places a boss-arena feature by name"},
        {"shrine.", "anchors the treasure shrine"},
        {"group.", "a name gameplay code can look up"},
    };
    const std::string& name = *entity.marker;
    for (const Known& known : kKnown) {
        if (name.rfind(known.prefix, 0) == 0) {
            ImGui::TextDisabled("%s", known.what);
            return;
        }
    }
    ImGui::TextDisabled("free name; the prefixes the game reads are:");
    for (const Known& known : kKnown)
        ImGui::TextDisabled("  %s", known.prefix);
}

// Everything that differs between one pick-from-a-list field and another.
// Enemy ids, pickup ids and script paths are the same widget wearing three
// vocabularies; before this they were two near-identical copies, and the second
// one drifted the moment it was written (its own placeholder, its own warning,
// no shared notion of what "unknown" means).
struct PickerSpec {
    const std::vector<std::string>* options = nullptr;
    // Shown in the closed combo when the value is empty. A fresh row is the one
    // moment the author cannot guess what the widget wants.
    const char* placeholder = "pick one";
    // printf format taking the value, shown when it is not in the list.
    const char* unknownFormat = "'%s' is not one the game defines";
    const char* emptyNote = "no list loaded -- typed values are not checked";
    // Called when the combo opens. Lets a list backed by the filesystem pick up
    // a file written since the editor started.
    std::function<void()> refresh;
};

// The widget half of a picker: assumes the caller has positioned the cursor and
// set the item width (a PropertyGrid row does both). Returns false when the
// value is not one the list knows, so the caller can place the warning where
// its own layout wants it.
bool pickerWidget(const char* id, std::string& value, const PickerSpec& spec,
                  InspectorContext& context)
{
    // A missing list must leave the field editable, only unguided: a broken
    // enemies.toml is not a reason the inspector cannot be typed into.
    if (!spec.options || spec.options->empty()) {
        stringInput(id, value, context);
        return true; // nothing to check against, so nothing to call unknown
    }

    const bool known = std::find(spec.options->begin(), spec.options->end(),
                                 value) != spec.options->end();
    const char* preview = value.empty() ? spec.placeholder : value.c_str();
    if (ImGui::BeginCombo(id, preview)) {
        if (spec.refresh)
            spec.refresh();
        for (const std::string& option : *spec.options) {
            if (ImGui::Selectable(option.c_str(), option == value)) {
                value = option;
                context.track(true, true);
            }
        }
        ImGui::EndCombo();
    }
    return known || value.empty();
}

// A labelled row whose valid values the game defines.
void vocabularyField(const char* label, std::string& value,
                     const std::vector<std::string>* options,
                     InspectorContext& context)
{
    PickerSpec spec;
    spec.options = options;
    const bool haveList = options && !options->empty();

    ui::PropertyGrid grid("##vocabulary");
    grid.row(label);
    char id[64];
    std::snprintf(id, sizeof(id), "##%s", label);
    const bool known = pickerWidget(id, value, spec, context);

    if (!haveList) {
        grid.full(spec.emptyNote);
    } else if (!known) {
        // Said here rather than left for the playtest. An id the game does not
        // know spawns nothing, silently, minutes later.
        grid.row("");
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), spec.unknownFormat,
                           value.c_str());
    }
}

void drawEnemySpawn(Entity& entity, InspectorContext& context)
{
    vocabularyField("enemy", *entity.enemySpawn, context.enemyIds, context);
}

void drawPickup(Entity& entity, InspectorContext& context)
{
    vocabularyField("pickup", *entity.pickup, context.pickupIds, context);
}

void drawTrigger(Entity& entity, InspectorContext& context)
{
    drawVec3Property("half_extents", "Half Extents", "m",
                     entity.trigger->size, glm::vec3(1.0f), 0.05f, context,
                     0.001f, 50.0f);
    ui::PropertyGrid grid("##trigger");
    stringRow(grid, "event", entity.trigger->event, context);
}

// Whether the light moves, drawn inside the light's own section: it is a
// property of this light, not a second thing an author has to remember to add.
void drawLightAnimation(LightAuthor& light, InspectorContext& context,
                        ui::PropertyGrid& grid)
{
    grid.row("animated");
    bool animated = light.animation.has_value();
    if (ImGui::Checkbox("##animated", &animated)) {
        if (animated)
            light.animation = game::content::LightAnimAuthor{};
        else
            light.animation.reset();
        context.track(true, true);
    }
    if (!light.animation)
        return;

    game::content::LightAnimAuthor& animation = *light.animation;
    grid.row("motion");
    int mode = int(animation.mode);
    if (ImGui::Combo("##motion", &mode, "steady\0flicker\0pulse\0"))
        animation.mode = game::content::LightAnimAuthor::Mode(mode);
    track(context);
    grid.row("rate", "/s");
    ImGui::DragFloat("##rate", &animation.speed, 0.05f, 0.0f, 30.0f, "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);
    track(context);
    grid.row("depth");
    ImGui::DragFloat("##depth", &animation.amount, 0.005f, 0.0f, 1.0f, "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);
    track(context);
    grid.row("phase", nullptr, "editor.inspector.light_phase",
             "Give each torch a different phase, or a row of them flickers in "
             "lockstep.");
    ImGui::DragFloat("##phase", &animation.phase, 0.05f, 0.0f, 10.0f, "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);
    track(context);
    grid.full("modulation only darkens -- the colour above is the brightest it "
              "gets");
}

// The lens, and nothing about where the camera is: position and facing are the
// entity's transform, edited with the gizmo like everything else.
void drawCamera(Entity& entity, InspectorContext& context)
{
    CameraAuthor& camera = *entity.camera;
    ui::PropertyGrid grid("##camera");

    grid.row("fov", "deg");
    ImGui::DragFloat("##fov", &camera.fovDegrees, 0.25f, 10.0f, 140.0f, "%.1f",
                     ImGuiSliderFlags_AlwaysClamp);
    track(context);

    // The framings this engine already uses. "60 degrees" means nothing until
    // it is next to the number the game itself is played at.
    struct Preset {
        const char* label;
        float fov;
        const char* note;
    };
    static const Preset kPresets[] = {
        {"game", 70.0f, "what the player sees"},
        {"editor", 65.0f, "the viewport's own"},
        {"portrait", 45.0f, "tight on one prop"},
        {"establishing", 90.0f, "the whole room"},
    };
    grid.row("framing");
    if (ImGui::BeginCombo("##framing", "pick a framing")) {
        for (const Preset& preset : kPresets) {
            if (ImGui::Selectable(preset.label)) {
                camera.fovDegrees = preset.fov;
                context.track(true, true);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%.0f deg -- %s", preset.fov, preset.note);
        }
        ImGui::EndCombo();
    }

    grid.row("near clip", "m");
    ImGui::DragFloat("##near", &camera.nearClip, 0.005f, 0.01f, 5.0f, "%.3f",
                     ImGuiSliderFlags_AlwaysClamp);
    track(context);
    grid.row("far clip", "m");
    ImGui::DragFloat("##far", &camera.farClip, 1.0f, 1.0f, 5000.0f, "%.0f",
                     ImGuiSliderFlags_AlwaysClamp);
    track(context);
    if (camera.farClip <= camera.nearClip) {
        grid.row("");
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "far must be beyond near, or nothing draws");
    }

    grid.row("active", nullptr, "editor.inspector.camera_active",
             "Unchecked parks this framing: it stays in the scene and the game "
             "will not look through it.");
    if (ImGui::Checkbox("##active", &camera.active))
        context.track(true, true);
    grid.row("priority", nullptr, "editor.inspector.camera_priority",
             "The highest active camera in the scene is the one the game looks "
             "through.");
    ImGui::DragInt("##priority", &camera.priority, 0.1f, -100, 100, "%d",
                   ImGuiSliderFlags_AlwaysClamp);
    track(context);
}

void drawAudio(Entity& entity, InspectorContext& context)
{
    game::content::AudioEmitterAuthor& audio = *entity.audio;

    {
        ui::PropertyGrid grid("##audio_source");
        stringRow(grid, "source", audio.source, context);
        if (context.audioAssets && !context.audioAssets->empty()) {
            const std::string current =
                audio.source.empty() ? "pick a clip" : audio.source;
            grid.row("browse");
            if (ImGui::BeginCombo("##clip_browser", current.c_str())) {
                for (const std::string& path : *context.audioAssets) {
                    if (ImGui::Selectable(path.c_str(),
                                          path == audio.source)) {
                        audio.source = path;
                        context.track(true, true);
                    }
                }
                ImGui::EndCombo();
            }
            if (!audio.source.empty() &&
                std::find(context.audioAssets->begin(),
                          context.audioAssets->end(),
                          audio.source) == context.audioAssets->end()) {
                grid.row("");
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                                   "clip is not in mounted audio assets");
            }
        } else {
            grid.full("no runtime audio clips found under assets/audio");
        }
    }

    const bool canPreview = !audio.source.empty() && context.requestAudioPreview;
    ImGui::BeginDisabled(!canPreview);
    if (ImGui::Button(context.audioPreviewing ? "Restart preview" : "Play preview"))
        context.requestAudioPreview(entity.id);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!context.audioPreviewing || !context.stopAudioPreview);
    if (ImGui::Button("Stop"))
        context.stopAudioPreview();
    ImGui::EndDisabled();

    static const eng::AudioBus kBuses[] = {
        eng::AudioBus::Music,   eng::AudioBus::Ambience,
        eng::AudioBus::Dialogue, eng::AudioBus::Weapons,
        eng::AudioBus::Sfx,     eng::AudioBus::Ui,
        eng::AudioBus::Warnings,
    };
    const eng::AudioBus selectedBus =
        audio.bus > static_cast<int>(eng::AudioBus::Master) &&
                audio.bus < static_cast<int>(eng::AudioBus::Count)
            ? static_cast<eng::AudioBus>(audio.bus)
            : eng::AudioBus::Sfx;
    ui::PropertyGrid grid("##audio");
    grid.row("bus");
    if (ImGui::BeginCombo("##bus", eng::audioBusName(selectedBus))) {
        for (eng::AudioBus bus : kBuses) {
            if (ImGui::Selectable(eng::audioBusName(bus), bus == selectedBus)) {
                audio.bus = static_cast<int>(bus);
                context.track(true, true);
            }
        }
        ImGui::EndCombo();
    }

    grid.row("gain", "dB");
    ImGui::SliderFloat("##gain", &audio.gainDb, -80.0f, 12.0f, "%.1f");
    track(context);
    grid.row("pitch");
    ImGui::SliderFloat("##pitch", &audio.pitch, 0.25f, 4.0f, "%.2fx");
    track(context);

    grid.row("3D spatial");
    if (ImGui::Checkbox("##spatial", &audio.spatialized))
        context.track(true, true);
    if (audio.spatialized) {
        grid.row("offset", "m");
        ImGui::DragFloat3("##offset", &audio.offset.x, 0.02f);
        track(context);
        grid.row("full volume to", "m");
        ImGui::DragFloat("##min_distance", &audio.minDistance, 0.05f, 0.0f,
                         1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        track(context);
        grid.row("inaudible after", "m");
        ImGui::DragFloat("##max_distance", &audio.maxDistance, 0.25f, 0.01f,
                         10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        track(context);
        if (audio.maxDistance <= audio.minDistance) {
            grid.row("");
            ImGui::TextColored(
                ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                "inaudible distance must exceed full-volume distance");
        }
        grid.row("rolloff");
        ImGui::SliderFloat("##rolloff", &audio.rolloff, 0.0f, 8.0f, "%.2f");
        track(context);
        grid.row("doppler");
        ImGui::SliderFloat("##doppler", &audio.dopplerFactor, 0.0f, 4.0f,
                           "%.2f");
        track(context);
        grid.full("the selected emitter shows its maximum reach in the "
                  "viewport");
    }

    int priorityIndex = 2;
    static const int kPriorities[] = {
        static_cast<int>(eng::AudioPriority::Background),
        static_cast<int>(eng::AudioPriority::Low),
        static_cast<int>(eng::AudioPriority::Normal),
        static_cast<int>(eng::AudioPriority::Important),
        static_cast<int>(eng::AudioPriority::Critical),
    };
    for (int index = 0; index < 5; ++index)
        if (audio.priority == kPriorities[index])
            priorityIndex = index;
    grid.row("priority");
    if (ImGui::Combo("##priority", &priorityIndex,
                     "background\0low\0normal\0important\0critical\0")) {
        audio.priority = kPriorities[priorityIndex];
        context.track(true, true);
    }

    grid.row("loop");
    if (ImGui::Checkbox("##loop", &audio.loop))
        context.track(true, true);
    grid.row("stream from disk");
    if (ImGui::Checkbox("##streaming", &audio.streaming))
        context.track(true, true);
    if (audio.streaming)
        grid.full("for long ambience and music, not repeated short SFX");
    grid.row("autostart");
    if (ImGui::Checkbox("##autostart", &audio.playing))
        context.track(true, true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Starts when the runtime scene materialises. Disable "
                          "when gameplay will control the component.");
    grid.row("may be voice-stolen");
    if (ImGui::Checkbox("##stealable", &audio.stealable))
        context.track(true, true);
}

// What the game will treat this entity as. The kind is not decoration: it is
// what decides which actions the sound table below offers, and it is what the
// runtime keys the conventional cue names off.
void drawActor(Entity& entity, InspectorContext& context)
{
    static const game::ActorKind kKinds[] = {
        game::ActorKind::Player, game::ActorKind::Npc, game::ActorKind::Enemy};
    const game::ActorKind current = *entity.actor;
    ui::PropertyGrid grid("##actor");
    grid.row("kind");
    if (ImGui::BeginCombo("##kind", game::actorKindName(current))) {
        for (game::ActorKind kind : kKinds) {
            if (ImGui::Selectable(game::actorKindName(kind), kind == current)) {
                entity.actor = kind;
                context.track(true, true);
            }
        }
        ImGui::EndCombo();
    }
    // The implied kinds, said out loud: an author who has already placed an
    // enemy spawn does not need this component at all, and finding that out
    // from the panel beats finding it out from a duplicate.
    if (const std::optional<game::ActorKind> implied = actorKindOf(entity);
        implied && entity.actor && *implied != *entity.actor) {
        ImGui::TextDisabled("its spawn component would say '%s' on its own",
                            game::actorKindName(*implied));
    }
    ImGui::TextDisabled("gates the Sounds table and names its default cues\n"
                        "(\"%s.hurt\", \"%s.death\", ...)",
                        game::actorKindCuePrefix(current),
                        game::actorKindCuePrefix(current));
}

// One cue per action this actor can perform.
//
// Every row is optional and empty by default: an empty row is "sound like the
// type says", not "silent". That distinction is the whole reason the table can
// be added to a placed entity without changing anything until a row is filled.
void drawSounds(Entity& entity, InspectorContext& context)
{
    game::ActorSoundSet& sounds = *entity.sounds;
    const std::optional<game::ActorKind> kind = actorKindOf(entity);
    if (!kind) {
        // The component outlived the thing that made it meaningful -- the enemy
        // spawn was removed, say. Kept and shown so it can be removed, rather
        // than silently cooked onto an entity that performs nothing.
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "this entity is not an actor any more");
        ImGui::TextDisabled("add an Actor component or remove this table");
        return;
    }

    ImGui::TextDisabled("empty = the cue this actor's type already plays");

    // One row per action: label, the cue, and a "..." that opens free text.
    //
    // The free-text field used to be on every row unconditionally, which made a
    // fourteen-action enemy twenty-eight rows tall -- so the actions at the
    // bottom (death, dodge) were below the fold of any inspector. Typing an id
    // by hand is the rare case (a cue authored since the editor started), so it
    // is one click away rather than always present.
    static std::string sTypingRow; // "<entity id>/<action id>", or empty
    const auto rowKey = [&](const game::ActorActionInfo& info) {
        return entity.id + "/" + info.id;
    };

    for (const game::ActorActionInfo& info : game::actorActions()) {
        if (!game::actorPerforms(*kind, info.action))
            continue; // a player does not telegraph; an enemy does not jump
        ImGui::PushID(info.id);
        std::string& cue = sounds.cues[static_cast<std::size_t>(info.action)];
        const std::string fallback =
            game::actorConventionCue(*kind, info.action);
        const bool unknown =
            !cue.empty() && context.audioCues && !context.audioCues->empty() &&
            std::find(context.audioCues->begin(), context.audioCues->end(),
                      cue) == context.audioCues->end();
        const bool typing = sTypingRow == rowKey(info) || unknown;

        const float buttonWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(-(ImGui::CalcTextSize(info.label).x +
                                  buttonWidth +
                                  ImGui::GetStyle().ItemSpacing.x * 3.0f));
        const std::string current = cue.empty() ? fallback + "  (default)" : cue;
        if (ImGui::BeginCombo("##cue", current.c_str())) {
            if (ImGui::Selectable("(default)", cue.empty())) {
                cue.clear();
                sTypingRow.clear();
                context.track(true, true);
            }
            if (context.audioCues) {
                for (const std::string& option : *context.audioCues) {
                    if (ImGui::Selectable(option.c_str(), option == cue)) {
                        cue = option;
                        sTypingRow.clear();
                        context.track(true, true);
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", info.hint);
        ImGui::SameLine();
        // Free text as well as the list: a cue authored after the editor
        // started must still be typeable, exactly as the clip field allows.
        if (ImGui::SmallButton(typing ? "v" : "..."))
            sTypingRow = typing ? std::string() : rowKey(info);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("type a cue id that is not in the list yet");
        ImGui::SameLine();
        ImGui::TextUnformatted(info.label);

        if (typing) {
            ImGui::SetNextItemWidth(-1.0f);
            stringField("##typed", cue, context);
            if (unknown) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                                   "'%s' is not a cue in audio.toml",
                                   cue.c_str());
            }
        }
        ImGui::PopID();
    }
    if (context.audioCues && context.audioCues->empty())
        ImGui::TextDisabled("no cue list loaded -- typed ids are not checked");
}

void drawAudioListener(Entity& entity, InspectorContext& context)
{
    game::content::AudioListenerAuthor& listener = *entity.audioListener;
    ui::PropertyGrid grid("##listener");
    grid.row("active");
    if (ImGui::Checkbox("##active", &listener.active))
        context.track(true, true);
    grid.row("priority");
    ImGui::DragInt("##priority", &listener.priority, 0.1f, -100, 100, "%d",
                   ImGuiSliderFlags_AlwaysClamp);
    track(context);
    grid.full("highest active listener wins; position and facing come from "
              "this entity's transform");
}

// The script a row runs, picked from what is on disk.
//
// A combo and not a text field, for the reason vocabularyField already gives
// about enemy ids: a path spelled from memory is a component that silently does
// nothing, found minutes later in the game with nothing pointing at the cause.
// The picker also spares the author the one detail they cannot guess -- that the
// stored path is *logical* ("scripts/door.lua"), not wherever the file sits on
// this machine.
//
// Falls back to a typed path only when nothing is on disk to offer, which is
// survivable rather than good: a first script has to be nameable before the
// list it would come from exists.
// The scripts vocabulary: the same picker the enemy and pickup fields use,
// pointed at the .lua files on disk.
PickerSpec scriptPickerSpec(InspectorContext& context)
{
    PickerSpec spec;
    spec.options = context.scriptPaths;
    spec.placeholder = "pick a script";
    spec.unknownFormat = "'%s' is not on disk";
    spec.emptyNote = "no scripts found under assets/scripts";
    spec.refresh = context.rescanScripts;
    return spec;
}

// A script prop that names another entity. The list is the open scene, so the
// picker offers exactly what the cooker will accept -- it fails the build on a
// name that is not in the document.
PickerSpec entityPickerSpec(InspectorContext& context)
{
    PickerSpec spec;
    spec.options = context.sceneEntityIds;
    spec.placeholder = "pick an entity";
    spec.unknownFormat = "'%s' is not in this scene";
    spec.emptyNote = "no entities to reference";
    return spec;
}

// The script path picker. Returns whether the path names something on disk;
// the caller places the warning, because only it knows where its row ends.
bool drawScriptPathPicker(std::string& path, InspectorContext& context)
{
    return pickerWidget("##path", path, scriptPickerSpec(context), context);
}

// Scripts get a hand-written block for the same reason they hand-write their
// serialiser: every other component is a fixed set of typed rows, and this is a
// reorderable list whose rows each carry a variable table of values.
void drawScriptProps(std::vector<game::content::ScriptPropAuthor>& props,
                     InspectorContext& context)
{
    using Prop = game::content::ScriptPropAuthor;
    static const char* kTypeNames[] = {"bool", "number", "string", "vec3",
                                       "entity"};
    int removeAt = -1;

    // A table, not SameLine with pixel widths. Props are a LIST of rows, and
    // the previous hand-measured 110/78/150 layout meant every row's key, type
    // and value started wherever the last one happened to end -- ragged down
    // the column, and crushed or overflowing at any dock width but the one it
    // was measured at. Stretch columns line them up at every width, which is
    // the same reason PropertyGrid is a table.
    constexpr ImGuiTableFlags kPropFlags = ImGuiTableFlags_SizingStretchProp |
                                           ImGuiTableFlags_NoSavedSettings |
                                           ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("##script_props", 4, kPropFlags)) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthStretch, 0.22f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        // The remove button is the one column with a real fixed size: it holds
        // one glyph and stretching it would leave a button the width of a name.
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFrameHeight());

        for (int i = 0; i < int(props.size()); ++i) {
            Prop& p = props[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            char key[96];
            std::snprintf(key, sizeof(key), "%s", p.key.c_str());
            if (ImGui::InputText("##key", key, sizeof(key)))
                p.key = key;
            track(context);

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            int typeIndex = int(p.type);
            if (ImGui::Combo("##type", &typeIndex, kTypeNames,
                             IM_ARRAYSIZE(kTypeNames)))
                p.type = Prop::Type(typeIndex);
            track(context);

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            switch (p.type) {
            case Prop::Type::Bool:
                // Checkbox ignores item width, so it would sit at the cell's
                // left edge while every other row's value box fills it. Centred
                // instead, so the column still reads as one column.
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                     (ImGui::GetContentRegionAvail().x -
                                      ImGui::GetFrameHeight()) *
                                         0.5f);
                ImGui::Checkbox("##v", &p.boolValue);
                break;
            case Prop::Type::Number:
                ImGui::DragFloat("##v", &p.numberValue, 0.01f);
                break;
            case Prop::Type::Vec3:
                ImGui::DragFloat3("##v", &p.vecValue.x, 0.01f);
                break;
            case Prop::Type::String:
                stringInput("##v", p.stringValue, context);
                break;
            case Prop::Type::Entity:
                // An entity reference is a string with a different meaning: it
                // names another entity, which the cooker checks and the host
                // resolves at start(). Picked from the scene rather than typed,
                // for the same reason the script path is.
                pickerWidget("##v", p.stringValue, entityPickerSpec(context),
                             context);
                break;
            }
            track(context);

            ImGui::TableNextColumn();
            if (ImGui::Button("x", ImVec2(-FLT_MIN, 0.0f)))
                removeAt = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Applied after the loop: erasing inside it would invalidate the reference
    // the current row is still holding.
    if (removeAt >= 0) {
        props.erase(props.begin() + removeAt);
        track(context);
    }
    if (ImGui::SmallButton("add prop")) {
        props.emplace_back();
        track(context);
    }
}

void drawScripts(Entity& entity, InspectorContext& context)
{
    int removeAt = -1;
    int moveFrom = -1;
    int moveTo = -1;

    // The row's controls, sized from the font rather than from four measured
    // constants. `-90.0f` for the path was a guess that left the arrows and the
    // remove button hanging off the edge in a narrow dock and floating in a
    // wide one; asking the style how big a button is works at any width and
    // survives a font change.
    const float button = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float removeWidth = ImGui::CalcTextSize("remove").x +
                              ImGui::GetStyle().FramePadding.x * 2.0f;
    const float controls = button * 3.0f + removeWidth + gap * 4.0f;

    for (int i = 0; i < int(entity.scripts.size()); ++i) {
        game::content::ScriptAuthor& script = entity.scripts[i];
        ImGui::PushID(i);
        ImGui::Separator();

        // The order is the order callbacks run in, so it is worth being able to
        // see and change.
        ImGui::Checkbox("##enabled", &script.enabled);
        track(context);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%d.", i + 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-controls);
        const bool pathKnown = drawScriptPathPicker(script.path, context);

        ImGui::SameLine();
        if (ImGui::ArrowButton("##up", ImGuiDir_Up) && i > 0) {
            moveFrom = i;
            moveTo = i - 1;
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##down", ImGuiDir_Down) &&
            i + 1 < int(entity.scripts.size())) {
            moveFrom = i;
            moveTo = i + 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("remove"))
            removeAt = i;

        // Under the row rather than beside it: a warning on the same line would
        // push the controls around as it appeared and disappeared.
        if (!pathKnown)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                               "'%s' is not on disk", script.path.c_str());

        drawScriptProps(script.props, context);
        ImGui::PopID();
    }

    // Both applied after the loop, for the same reason as the props above.
    if (removeAt >= 0) {
        entity.scripts.erase(entity.scripts.begin() + removeAt);
        track(context);
    } else if (moveFrom >= 0) {
        std::swap(entity.scripts[moveFrom], entity.scripts[moveTo]);
        track(context);
    }

    ImGui::Separator();
    if (ImGui::Button("add script")) {
        entity.scripts.emplace_back();
        track(context);
    }
    ImGui::TextDisabled("props reach the script as self.props");
}

void drawSpin(Entity& entity, InspectorContext& context)
{
    SpinAuthor& spin = *entity.spin;
    {
        ui::PropertyGrid grid("##spin");
        grid.row("rate", "deg/s");
        ImGui::DragFloat("##rate", &spin.degreesPerSecond, 1.0f, -720.0f,
                         720.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        track(context);
    }
    drawVec3Property("axis", "Axis", "direction", spin.axis,
                     glm::vec3(0.0f, 1.0f, 0.0f), 0.02f, context, -1.0f,
                     1.0f);
    if (glm::length(spin.axis) <= 0.0f)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "a zero axis spins nothing");
    ImGui::TextDisabled("children spin with it -- park a camera under this "
                        "for an orbit");
}

void drawShader(Entity& entity, InspectorContext& context)
{
    game::content::ShaderAuthor& sh = *entity.shader;
    // These params modulate the entity's material, so the material swatch is
    // the subject here too -- the panel above already has it pointed at this
    // entity, so this only draws it.
    if (context.previewTexture != 0) {
        ImGui::Image(static_cast<ImTextureID>(context.previewTexture),
                     ImVec2(96.0f, 96.0f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("tint and rim modulate");
        ImGui::TextDisabled("the material shown here");
        ImGui::EndGroup();
    }
    {
        ui::PropertyGrid grid("##shader");
        grid.row("tint");
        ImGui::ColorEdit3("##tint", &sh.tint.x);
        track(context);
        grid.row("opacity");
        ImGui::SliderFloat("##opacity", &sh.opacity, 0.0f, 1.0f, "%.2f");
        track(context);
        grid.row("rim colour");
        ImGui::ColorEdit3("##rim", &sh.rimColour.x);
        track(context);
        grid.row("rim strength");
        ImGui::SliderFloat("##rim_strength", &sh.rimStrength, 0.0f, 4.0f,
                           "%.2f");
        track(context);
        grid.row("rim power");
        ImGui::SliderFloat("##rim_power", &sh.rimPower, 0.25f, 16.0f, "%.2f");
        track(context);
        grid.row("cutout");
        ImGui::SliderFloat("##cutout", &sh.alphaScissor, 0.0f, 1.0f, "%.2f");
        track(context);
    }

    // The cost, stated where the decision is made. This is the one component
    // whose price is a draw call rather than a few bytes, and an author who
    // puts it on a hundred walls should learn that here rather than from a
    // frame-time graph.
    ImGui::TextDisabled("gives this entity its own copy of its material:\n"
                        "one more draw call. For hero objects, not for walls.");
    if (sh.opacity < 1.0f)
        ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.38f, 1.0f),
                           "opacity needs a blending material to show");
}

void drawParticles(Entity& entity, InspectorContext& context)
{
    game::content::ParticleAuthor& fx = *entity.particles;
    // A combo over the library rather than a text field: an effect name that
    // does not resolve plays nothing, silently, and the author has no way to
    // find out which names exist.
    ui::PropertyGrid effectGrid("##fx_effect");
    if (context.particleEffects && !context.particleEffects->empty()) {
        const std::string current = fx.effect.empty() ? "(none)" : fx.effect;
        effectGrid.row("effect");
        if (ImGui::BeginCombo("##effect", current.c_str())) {
            if (ImGui::Selectable("(none)", fx.effect.empty())) {
                fx.effect.clear();
                context.track(true, true);
            }
            for (const std::string& name : *context.particleEffects) {
                if (ImGui::Selectable(name.c_str(), name == fx.effect)) {
                    fx.effect = name;
                    context.track(true, true);
                }
                previewTooltip(context, context.requestEffectPreview, name,
                               nullptr);
            }
            ImGui::EndCombo();
        }
    } else {
        stringRow(effectGrid, "effect", fx.effect, context);
        effectGrid.full("no effect library loaded");
    }
    // The assigned effect, running. A particle is motion, so a still frame of
    // the name tells an author nothing about whether it is the one they meant.
    if (!fx.effect.empty() && context.requestEffectPreview) {
        context.requestEffectPreview(fx.effect);
        if (context.previewTexture != 0)
            ImGui::Image(static_cast<ImTextureID>(context.previewTexture),
                         ImVec2(112.0f, 112.0f));
    }
    drawVec3Property("offset", "Offset", "m", fx.offset, glm::vec3(0.0f),
                     0.02f, context);
    ui::PropertyGrid grid("##fx");
    grid.full("local to the entity; the viewport mark shows the emission "
              "origin");
    grid.row("size scale");
    ImGui::SliderFloat("##fx_scale", &fx.scale, 0.01f, 16.0f, "%.2fx");
    track(context);
    grid.row("autostart", nullptr, "editor.inspector.fx_autostart",
             "Starts when the scene materialises. Disable for an effect that "
             "gameplay will trigger explicitly.");
    ImGui::Checkbox("##fx_autostart", &fx.playing);
    track(context);
}

void drawPortal(Entity& entity, InspectorContext& context)
{
    PortalAuthor& portal = *entity.portal;
    // Driven straight off the component's field table, so a uniform the shader
    // gains appears here with no edit to this function -- the whole point of
    // the reflection layer. The grid gives it the same two columns everything
    // else has; a generic drawer that looked different from the hand-written
    // ones would announce that it is generated, which is nobody's business.
    ui::PropertyGrid grid("##portal", 0.46f);
    const eng::FieldSpan fields = eng::fieldsOf<PortalAuthor>();
    for (int i = 0; i < fields.count; ++i) {
        const eng::Field& field = fields.data[i];
        void* value = eng::fieldPtr(&portal, field);
        char id[80];
        std::snprintf(id, sizeof(id), "##%s", field.name);
        if (field.type == eng::FieldType::Colour) {
            grid.row(field.name);
            ImGui::ColorEdit3(id, &static_cast<glm::vec3*>(value)->x,
                              ImGuiColorEditFlags_Float);
            track(context);
            continue;
        }
        if (field.type != eng::FieldType::Float)
            continue;
        float& number = *static_cast<float*>(value);
        grid.row(field.name);
        if (field.max > field.min)
            ImGui::DragFloat(id, &number, 0.01f, field.min, field.max, "%.3f",
                             ImGuiSliderFlags_AlwaysClamp);
        else
            ImGui::DragFloat(id, &number, 0.01f);
        track(context);
    }
}

// The player, authored on the camera that is their eye. Both drawers edit the
// runtime components directly (mirror-not-translate), so what the inspector
// shows, what the .scn stores and what the game reads are one set of numbers.
// The in-game console's Viewmodel tab (F1) tunes the same fields live and
// copies them back out as TOML; this is where a *level* pins its own.
void drawFirstPerson(Entity& entity, InspectorContext& context)
{
    game::content::FirstPersonAuthor& player = *entity.firstPerson;
    {
        ui::PropertyGrid grid("##fp_active");
        grid.row("active", nullptr, "editor.inspector.fp_active",
                 "Off keeps the tuning in the scene without applying it -- the "
                 "game falls back to its config defaults.");
        ImGui::Checkbox("##active", &player.active);
        track(context);
    }

    ImGui::SeparatorText("movement");
    {
        ui::PropertyGrid grid("##fp_move");
        grid.row("move speed", "m/s");
        ImGui::SliderFloat("##move_speed", &player.moveSpeed, 0.5f, 20.0f,
                           "%.2f");
        track(context);
        grid.row("mouse sensitivity", "rad/px");
        ImGui::SliderFloat("##sensitivity", &player.mouseSensitivity, 0.0002f,
                           0.02f, "%.4f");
        track(context);
    }

    ImGui::SeparatorText("lens");
    {
        ui::PropertyGrid grid("##fp_lens");
        grid.row("base fov", "deg");
        ImGui::SliderFloat("##base_fov", &player.baseFovDegrees, 40.0f, 130.0f,
                           "%.1f");
        track(context);
        grid.row("sprint fov kick", "deg");
        ImGui::SliderFloat("##sprint_fov", &player.sprintFovKick, 0.0f, 25.0f,
                           "%.1f");
        track(context);
        grid.row("head bob", "m");
        ImGui::SliderFloat("##bob", &player.bobAmount, 0.0f, 0.2f, "%.3f");
        track(context);
        grid.row("head bob speed");
        ImGui::SliderFloat("##bob_speed", &player.bobSpeed, 0.0f, 25.0f);
        track(context);
        grid.full("the camera moves subtly; the viewmodel moves loudly");
    }
}

// The other two camera shapes. Same rule as the first-person drawer above:
// they edit the runtime component directly, so what the inspector shows, what
// the .scn stores and what the game reads are one set of numbers.
void drawThirdPerson(Entity& entity, InspectorContext& context)
{
    game::content::ThirdPersonAuthor& camera = *entity.thirdPerson;
    {
        ui::PropertyGrid grid("##tp_active");
        grid.row("active", nullptr, "editor.inspector.tp_active",
                 "Off keeps the framing in the scene without applying it.");
        ImGui::Checkbox("##active", &camera.active);
        track(context);
    }

    ImGui::SeparatorText("boom");
    {
        ui::PropertyGrid grid("##tp_boom");
        grid.row("distance", "m");
        ImGui::SliderFloat("##distance", &camera.distance, 0.5f, 12.0f, "%.2f");
        track(context);
        grid.row("pivot height", "m");
        ImGui::SliderFloat("##pivot", &camera.pivotHeight, 0.0f, 3.0f, "%.2f");
        track(context);
        grid.row("shoulder", "m");
        ImGui::SliderFloat("##shoulder", &camera.shoulderOffset, -2.0f, 2.0f,
                           "%.2f");
        track(context);
        grid.row("fov", "deg");
        ImGui::SliderFloat("##tp_fov", &camera.fovDegrees, 40.0f, 130.0f,
                           "%.1f");
        track(context);
        grid.full("chest-height pivot keeps the head off the top of the "
                  "screen when the camera looks down");
    }

    ImGui::SeparatorText("follow");
    {
        ui::PropertyGrid grid("##tp_follow");
        grid.row("horizontal", "1/s");
        ImGui::SliderFloat("##follow", &camera.followRate, 1.0f, 60.0f, "%.1f");
        track(context);
        grid.row("vertical", "1/s");
        ImGui::SliderFloat("##follow_v", &camera.followRateVertical, 1.0f,
                           60.0f, "%.1f");
        track(context);
        grid.row("turn rate", "deg/s");
        ImGui::SliderFloat("##turn", &camera.turnRateDegrees, 90.0f, 2000.0f,
                           "%.0f");
        track(context);
        grid.full("vertical slower than horizontal: stairs pump a camera that "
                  "tracks height exactly");
    }

    ImGui::SeparatorText("look");
    {
        ui::PropertyGrid grid("##tp_look");
        grid.row("pitch min", "deg");
        ImGui::SliderFloat("##pitch_min", &camera.pitchMinDegrees, -89.0f, 0.0f,
                           "%.0f");
        track(context);
        grid.row("pitch max", "deg");
        ImGui::SliderFloat("##pitch_max", &camera.pitchMaxDegrees, 0.0f, 89.0f,
                           "%.0f");
        track(context);
        grid.row("sensitivity", "rad/px");
        ImGui::SliderFloat("##tp_sens", &camera.mouseSensitivity, 0.0002f,
                           0.02f, "%.4f");
        track(context);
    }

    ImGui::SeparatorText("spring arm");
    {
        ui::PropertyGrid grid("##tp_arm");
        grid.row("radius", "m");
        ImGui::SliderFloat("##arm_radius", &camera.collisionRadius, 0.0f, 1.0f,
                           "%.2f");
        track(context);
        grid.row("push out", "m/s");
        ImGui::SliderFloat("##push_out", &camera.pushOutSpeed, 0.5f, 30.0f,
                           "%.1f");
        track(context);
        grid.row("minimum", "m");
        ImGui::SliderFloat("##min_distance", &camera.minDistance, 0.1f, 4.0f,
                           "%.2f");
        track(context);
        grid.full("comes in instantly, goes back out at push out -- the "
                  "asymmetry is what stops a doorway strobing");
    }

    ImGui::SeparatorText("lock-on");
    {
        ui::PropertyGrid grid("##tp_lock");
        grid.row("framing bias");
        ImGui::SliderFloat("##bias", &camera.lockFramingBias, 0.0f, 1.0f,
                           "%.2f");
        track(context);
        grid.row("blend rate", "1/s");
        ImGui::SliderFloat("##blend", &camera.lockBlendRate, 1.0f, 30.0f,
                           "%.1f");
        track(context);
        grid.row("pitch", "deg");
        ImGui::SliderFloat("##lock_pitch", &camera.lockPitchDegrees, -45.0f,
                           15.0f, "%.1f");
        track(context);
        grid.row("distance boost", "m");
        ImGui::SliderFloat("##lock_boost", &camera.lockDistanceBoost, 0.0f,
                           6.0f, "%.2f");
        track(context);
        grid.full("0 frames the player, 1 frames the target");
    }
}

void drawScreen(Entity& entity, InspectorContext& context)
{
    game::content::ScreenAuthor& screen = *entity.screen;
    ImGui::TextDisabled("This scene is a 2D screen: entities are authored in\n"
                        "virtual pixels on the page plane.");
    {
        ui::PropertyGrid grid("##screen_page");
        grid.row("active");
        ImGui::Checkbox("##screen_active", &screen.active);
        track(context);
        grid.row("page width", "px");
        ImGui::DragFloat("##page_w", &screen.pageWidth, 1.0f, 16.0f, 4096.0f,
                         "%.0f");
        track(context);
        grid.row("page height", "px");
        ImGui::DragFloat("##page_h", &screen.pageHeight, 1.0f, 16.0f, 4096.0f,
                         "%.0f");
        track(context);
        grid.full("the height is what always fills the screen");
    }

    ImGui::SeparatorText("fit");
    {
        ui::PropertyGrid grid("##screen_fit");
        grid.row("aspect");
        const char* fits[] = {"Height", "Contain"};
        ImGui::Combo("##fit", &screen.fit, fits, IM_ARRAYSIZE(fits));
        track(context);
        grid.row("origin");
        const char* origins[] = {"Centre", "TopLeft"};
        ImGui::Combo("##origin", &screen.origin, origins,
                     IM_ARRAYSIZE(origins));
        track(context);
        grid.full(screen.fit == eng::ecs::ScreenCamera::Contain
                      ? "the whole page is always visible, letterboxed"
                      : "a wide window sees past the page's sides");
    }

    ImGui::SeparatorText("depth");
    {
        ui::PropertyGrid grid("##screen_depth");
        grid.row("layer spacing", "u");
        ImGui::SliderFloat("##layer", &screen.layerSpacing, 0.0f, 8.0f, "%.2f");
        track(context);
        grid.row("fov", "deg");
        ImGui::SliderFloat("##screen_fov", &screen.fovDegrees, 5.0f, 120.0f,
                           "%.1f");
        track(context);
        grid.full("the projection stays perspective, so a layer off the page "
                  "plane is very slightly scaled -- that is the depth cue");
    }
}

// The preview is the answer to "is the weapon actually in the hand": a question
// nobody could ask in the editor before, because the hands were not drawn here
// at all. Deliberately two fields -- it selects what to look at, it does not
// tune anything. The seating numbers belong to the weapon (weapons.toml), and
// the framing belongs to the rig above.
void drawViewmodelPreview(Entity& entity, InspectorContext& context)
{
    game::content::ViewmodelPreviewAuthor& preview = *entity.viewmodelPreview;
    ui::PropertyGrid grid("##viewmodel_preview");
    grid.row("weapon");
    PickerSpec spec;
    spec.options = context.weaponIds;
    spec.placeholder = "slot 0";
    spec.unknownFormat = "'%s' is not a weapon weapons.toml defines";
    spec.emptyNote = "weapons.toml not found -- typed ids are not checked";
    const bool known =
        pickerWidget("##vm_weapon", preview.weapon, spec, context);
    grid.row("visible");
    ImGui::Checkbox("##vm_visible", &preview.visible);
    track(context);
    if (!known) {
        grid.row("");
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), spec.unknownFormat,
                           preview.weapon.c_str());
    }
    grid.full("editor only -- the cook drops this, the map never carries it");
    grid.full("seat the weapon in the hand with the game's F1 Viewmodel panel");
}

void drawViewmodelRig(Entity& entity, InspectorContext& context)
{
    game::content::ViewmodelRigAuthor& rig = *entity.viewmodelRig;
    ImGui::TextDisabled("Camera space: +x right, +y up, -z forward.");
    drawVec3Property("offset", "Socket", "m", rig.offset,
                     game::content::ViewmodelRigAuthor{}.offset, 0.005f, context);
    drawVec3Property("rotation", "Rotation", "deg", rig.rotation,
                     game::content::ViewmodelRigAuthor{}.rotation, 0.25f, context);
    {
        ui::PropertyGrid grid("##rig");
        grid.row("scale");
        ImGui::SliderFloat("##scale", &rig.scale, 0.05f, 3.0f, "%.3f");
        track(context);
        grid.row("motion enabled", nullptr, "editor.inspector.rig_motion",
                 "Off freezes bob, sway, recoil and the landing dip at the "
                 "socket pose.");
        ImGui::Checkbox("##motion", &rig.motionEnabled);
        track(context);
    }

    ImGui::SeparatorText("layer strength");
    {
        ui::PropertyGrid grid("##rig_layers");
        grid.full("multipliers over each weapon's own feel numbers");
        grid.row("bob");
        ImGui::SliderFloat("##bob_scale", &rig.bobScale, 0.0f, 4.0f, "x%.2f");
        track(context);
        grid.row("sway");
        ImGui::SliderFloat("##sway_scale", &rig.swayScale, 0.0f, 4.0f, "x%.2f");
        track(context);
        grid.row("recoil");
        ImGui::SliderFloat("##recoil_scale", &rig.recoilScale, 0.0f, 4.0f,
                           "x%.2f");
        track(context);
    }

    ImGui::SeparatorText("bob");
    {
        ui::PropertyGrid grid("##rig_bob");
        grid.row("reference speed", "m/s", "editor.inspector.rig_reference",
                 "Player speed the walk cycle is normalised against, so "
                 "retuning move speed does not retune the bob.");
        ImGui::SliderFloat("##reference_speed", &rig.bobReferenceSpeed, 1.0f,
                           14.0f, "%.1f");
        track(context);
        grid.row("roll", "deg");
        ImGui::SliderFloat("##bob_roll", &rig.bobRollDegrees, 0.0f, 12.0f,
                           "%.2f");
        track(context);
    }

    ImGui::SeparatorText("look sway");
    {
        ui::PropertyGrid grid("##rig_sway");
        grid.row("return speed");
        ImGui::SliderFloat("##sway_return", &rig.swayReturn, 0.0f, 30.0f);
        track(context);
        grid.row("max offset", "m");
        ImGui::SliderFloat("##sway_max", &rig.swayMax, 0.0f, 0.25f, "%.3f");
        track(context);
        grid.row("roll", "deg");
        ImGui::SliderFloat("##sway_roll", &rig.swayRollDegrees, 0.0f, 15.0f,
                           "%.2f");
        track(context);
    }

    ImGui::SeparatorText("landing");
    {
        ui::PropertyGrid grid("##rig_landing");
        grid.row("dip", "m");
        ImGui::SliderFloat("##dip", &rig.landingDip, 0.0f, 0.3f, "%.3f");
        track(context);
        grid.row("recovery");
        ImGui::SliderFloat("##recovery", &rig.landingRecovery, 0.5f, 30.0f);
        track(context);
        grid.full("tune live in the game's Viewmodel panel (F1), then pin the "
                  "result here or in game.toml");
    }
}

void drawOrbit(Entity& entity, InspectorContext& context)
{
    OrbitAuthor& orbit = *entity.orbit;
    drawVec3Property("centre", "Centre", "m", orbit.centre,
                     glm::vec3(0.0f), 0.05f, context);
    if (ImGui::Button("Centre on the origin")) {
        orbit.centre = glm::vec3(0.0f);
        context.track(true, true);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(in this entity's own frame)");

    {
        ui::PropertyGrid grid("##orbit");
        grid.row("radius", "m");
        ImGui::DragFloat("##radius", &orbit.radius, 0.05f, 0.0f, 200.0f,
                         "%.2f", ImGuiSliderFlags_AlwaysClamp);
        track(context);
        grid.row("rate", "deg/s");
        ImGui::DragFloat("##rate", &orbit.degreesPerSecond, 1.0f, -720.0f,
                         720.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        track(context);
        // A rate an author can judge: "one turn every twelve seconds" is a
        // decision about pacing, where "30 deg/s" is arithmetic.
        if (orbit.degreesPerSecond != 0.0f) {
            char lap[64];
            std::snprintf(lap, sizeof(lap), "one lap every %.1f s",
                          double(360.0f / std::abs(orbit.degreesPerSecond)));
            grid.full(lap);
        }
        grid.row("height", "m");
        ImGui::DragFloat("##height", &orbit.height, 0.05f, -50.0f, 50.0f,
                         "%.2f", ImGuiSliderFlags_AlwaysClamp);
        track(context);
        grid.row("phase", "deg", "editor.inspector.orbit_phase",
                 "Where on the ring it starts. Two things on one ring with no "
                 "phase sit on top of each other.");
        ImGui::DragFloat("##phase", &orbit.phaseDegrees, 1.0f, 0.0f, 360.0f,
                         "%.0f", ImGuiSliderFlags_AlwaysClamp);
        track(context);
    }

    ImGui::Separator();
    drawVec3Property("axis", "Axis", "direction", orbit.axis,
                     glm::vec3(0.0f, 1.0f, 0.0f), 0.02f, context, -1.0f,
                     1.0f);
    if (glm::length(orbit.axis) <= 0.0f)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "a zero axis is not a ring");

    int facing = int(orbit.facing);
    ui::PropertyGrid facingGrid("##orbit_facing");
    facingGrid.row("facing");
    if (ImGui::Combo("##facing", &facing,
                     "free\0look at centre\0look along travel\0"))
        orbit.facing = OrbitAuthor::Facing(facing);
    track(context);
    switch (orbit.facing) {
    case OrbitAuthor::Facing::Free:
        ImGui::TextDisabled("keeps its own rotation -- add Spin and it turns\n"
                            "on its own axis while it travels");
        break;
    case OrbitAuthor::Facing::Centre:
        ImGui::TextDisabled("aimed at the centre: what a camera circling a\n"
                            "subject wants. Overrides Spin's rotation.");
        break;
    case OrbitAuthor::Facing::Travel:
        ImGui::TextDisabled("aimed along the ring, like something flying it.\n"
                            "Overrides Spin's rotation.");
        break;
    }
}

struct Drawer {
    const char* id; // ComponentType::id
    void (*draw)(Entity& entity, InspectorContext& context);
};

constexpr Drawer kDrawers[] = {
    {"prefab", drawMesh},
    {"mesh", drawMeshAsset},
    {"primitive", drawPrimitive},
    {"cell", drawCell},
    {"collider", drawCollider},
    {"light", drawLight},
    {"camera", drawCamera},
    {"first_person", drawFirstPerson},
    {"third_person", drawThirdPerson},
    {"screen", drawScreen},
    {"viewmodel_rig", drawViewmodelRig},
    {"viewmodel_preview", drawViewmodelPreview},
    {"audio", drawAudio},
    {"audio_listener", drawAudioListener},
    {"actor", drawActor},
    {"sounds", drawSounds},
    {"scripts", drawScripts},
    {"spin", drawSpin},
    {"orbit", drawOrbit},
    {"shader", drawShader},
    {"particles", drawParticles},
    {"portal", drawPortal},
    {"player_spawn", drawPlayerSpawn},
    {"exit", drawExit},
    {"marker", drawMarker},
    {"enemy_spawn", drawEnemySpawn},
    {"pickup", drawPickup},
    {"trigger", drawTrigger},
};

} // namespace

bool drawPrimitiveFields(eng::ecs::PrimitiveMesh& mesh, ui::PropertyGrid& grid,
                         InspectorContext* context)
{
    using P = eng::ecs::PrimitiveMesh;
    bool edited = false;
    // One place to close over both callers' notions of "something changed": the
    // inspector needs the drag/commit pair so a scrub is one undo entry, the
    // browser only needs to know the draft moved.
    const auto committed = [&] {
        if (context)
            track(*context);
        edited = edited || ImGui::IsItemEdited();
    };

    grid.row("kind");
    const char* const* names = eng::ecs::primitiveKindNames();
    if (ImGui::BeginCombo("##kind", eng::ecs::primitiveKindName(mesh.kind))) {
        for (int i = 0; i < P::KindCount; ++i) {
            if (ImGui::Selectable(names[i], mesh.kind == i)) {
                mesh.kind = i;
                if (context)
                    context->track(true, true);
                edited = true;
            }
        }
        ImGui::EndCombo();
    }

    // Which parameters exist is a property of the kind, and the generators
    // ignore the rest. Drawing them all would offer a sphere a `size` that does
    // nothing, which is worse than not offering it.
    const bool usesSize =
        mesh.kind == P::Box || mesh.kind == P::BeveledBox || mesh.kind == P::Plane;
    const bool round = mesh.kind == P::Sphere || mesh.kind == P::Capsule ||
                       mesh.kind == P::Cylinder || mesh.kind == P::Cone ||
                       mesh.kind == P::Disc;
    const bool usesHeight = mesh.kind == P::Capsule ||
                            mesh.kind == P::Cylinder || mesh.kind == P::Cone;

    if (usesSize) {
        grid.row("size", "m");
        ImGui::DragFloat3("##size", &mesh.size.x, 0.02f, 0.001f, 200.0f, "%.3f",
                          ImGuiSliderFlags_AlwaysClamp);
        committed();
    }
    if (round) {
        grid.row("radius", "m");
        ImGui::DragFloat("##radius", &mesh.radius, 0.01f, 0.001f, 100.0f,
                         "%.3f", ImGuiSliderFlags_AlwaysClamp);
        committed();
    }
    if (usesHeight) {
        grid.row("height", "m");
        ImGui::DragFloat("##height", &mesh.height, 0.01f, 0.001f, 100.0f,
                         "%.3f", ImGuiSliderFlags_AlwaysClamp);
        committed();
    }
    if (mesh.kind == P::BeveledBox) {
        grid.row("bevel", nullptr, "editor.inspector.bevel",
                 "How far the edges are cut back, as a share of the box. Above "
                 "0.5 there is no box left.");
        ImGui::SliderFloat("##bevel", &mesh.bevel, 0.0f, 0.5f);
        committed();
    }
    if (round) {
        grid.row("segments", nullptr, "editor.inspector.segments",
                 "Radial subdivisions. This engine renders low-poly on "
                 "purpose -- 8 to 16 usually reads better than 64.");
        ImGui::SliderInt("##segments", &mesh.segments, 3, 128);
        committed();
        grid.row("rings");
        ImGui::SliderInt("##rings", &mesh.rings, 2, 64);
        committed();
    }
    grid.row("inward facing", nullptr, "editor.inspector.inward_facing",
             "Flips the winding and the normals: a box you stand inside rather "
             "than one you walk around. How a room is blocked out.");
    if (ImGui::Checkbox("##inward", &mesh.inwardFacing)) {
        if (context)
            context->track(true, true);
        edited = true;
    }
    return edited;
}

void drawEntityIdentity(Entity& entity, InspectorContext& context)
{
    ImGui::Text("id      %s", entity.id.c_str());
    stringField("name", entity.name, context);
    if (!entity.parent.empty()) {
        // Read-only here on purpose: changing a parent has to re-express the
        // transform so the entity stays where it is drawn, and that is what the
        // outliner's drag and the context menu do. A text field would silently
        // teleport it.
        ImGui::TextDisabled("parent  %s", entity.parent.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("drag rows in the Outliner to change this");
    }

    // Labelled "local" only when there is a parent to be local to, because for
    // every other entity in the scene it is simply the world and saying so
    // would be noise.
    ImGui::SeparatorText(entity.parent.empty() ? "Transform"
                                               : "Transform (Local to Parent)");
    ImGui::TextDisabled("Drag to scrub; click to type; Tab moves between axes.");
    drawVec3Property("position", "Position", "m", entity.transform.position,
                     glm::vec3(0.0f), 0.05f, context);
    drawVec3Property("rotation", "Rotation", "deg",
                     entity.transform.rotationDegrees, glm::vec3(0.0f), 1.0f,
                     context, 0.0f, 0.0f, "%.1f");
    drawVec3Property("scale", "Scale", "factor", entity.transform.scale,
                     glm::vec3(1.0f), 0.01f, context, 0.001f, 100.0f);
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
