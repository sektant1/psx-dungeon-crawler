#include "ComponentInspector.h"

#include <imgui.h>

#include <algorithm>
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
using game::content::SpinAuthor;

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
        // The classified catalogue when it is available: the combo used to
        // offer every material the renderer holds, including the compositor
        // passes and particle materials that cannot draw an entity at all.
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
                if (advice.fit != Fit::Good) {
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", advice.reason.c_str());
                }
            }
        } else if (context.materialNames) {
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
    // What the override is doing to this piece, stated on the panel that set
    // it. An override that renders wrongly is otherwise invisible until the
    // level is looked at from the right angle.
    if (!entity.material.empty() && context.materials) {
        for (const MaterialInfo& info : *context.materials) {
            if (info.name != entity.material)
                continue;
            const MaterialAdvice advice =
                materialFits(info.klass, context.meshKind);
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

// A light authored for this renderer: a hue, an energy, and a reach.
//
// The colour a light carries is pre-multiplied by its energy -- a torch is
// authored well above 1.0 so the bloom pass catches it -- which makes a plain
// RGB picker the wrong control. Dragging "brightness" up used to mean opening
// the picker and multiplying three numbers by hand, and the hue drifted every
// time. Hue and energy are separated here and recombined on the way out, which
// is how every engine that has an HDR light exposes one.
void drawLightAnimation(LightAuthor& light, InspectorContext& context);

void drawLight(Entity& entity, InspectorContext& context)
{
    LightAuthor& light = *entity.light;
    int type = light.type == LightAuthor::Type::Directional ? 0 : 1;
    if (ImGui::Combo("type", &type, "directional\0point\0"))
        light.type = type == 0 ? LightAuthor::Type::Directional
                               : LightAuthor::Type::Point;
    track(context);

    float energy = std::max({light.colour.r, light.colour.g, light.colour.b});
    glm::vec3 hue = energy > 1e-4f ? light.colour / energy : glm::vec3(1.0f);
    bool recombine = false;

    if (ImGui::ColorEdit3("colour", &hue.x, ImGuiColorEditFlags_Float))
        recombine = true;
    track(context);
    if (ImGui::DragFloat("brightness", &energy, 0.02f, 0.0f, 20.0f, "%.2f"))
        recombine = true;
    track(context);
    if (recombine)
        light.colour = hue * std::max(energy, 0.0f);
    if (energy > 1.0f)
        ImGui::TextDisabled("above 1.0 -- feeds the bloom pass");

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
    if (ImGui::BeginCombo("preset", "pick a look")) {
        for (const Preset& preset : kPresets) {
            if (ImGui::Selectable(preset.label)) {
                light.colour = preset.colour;
                if (light.type == LightAuthor::Type::Point)
                    light.range = preset.range;
                context.track(true, true);
            }
            ImGui::SameLine();
            ImGui::ColorButton(
                preset.label,
                ImVec4(preset.colour.r, preset.colour.g, preset.colour.b, 1.0f),
                ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f, 14.0f));
        }
        ImGui::EndCombo();
    }

    if (light.type == LightAuthor::Type::Point) {
        ImGui::DragFloat("range", &light.range, 0.25f, 0.0f, 200.0f, "%.2f m");
        track(context);
        // The kit's cell is 4 m and its rooms are 3 m tall, so a reach stated
        // in cells is the number that answers "does this cross the doorway".
        ImGui::TextDisabled("%.1f cells across", double(light.range * 2.0f / 4.0f));
    } else {
        ImGui::TextDisabled("aimed by the entity's rotation, not placed");
    }
    if (ImGui::Checkbox("light casts shadows", &light.castShadows))
        context.track(true, true);
    if (light.castShadows)
        ImGui::TextDisabled("stencil shadows, from opted-in casters only");
    ImGui::Separator();
    drawLightAnimation(light, context);
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

// The prefixes the runtime actually looks up. A marker whose name matches none
// of them is inert -- valid, saved, cooked, and read by nothing.
void drawMarker(Entity& entity, InspectorContext& context)
{
    stringField("marker", *entity.marker, context);

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

// A field whose valid values the game defines. Falls back to free text when
// the list is unavailable -- a missing enemies.toml must not make the field
// uneditable, only unguided.
void vocabularyField(const char* label, std::string& value,
                     const std::vector<std::string>* options,
                     InspectorContext& context)
{
    if (!options || options->empty()) {
        stringField(label, value, context);
        ImGui::TextDisabled("no list loaded -- typed ids are not checked");
        return;
    }
    const bool known =
        std::find(options->begin(), options->end(), value) != options->end();
    if (ImGui::BeginCombo(label, value.c_str())) {
        for (const std::string& option : *options) {
            if (ImGui::Selectable(option.c_str(), option == value)) {
                value = option;
                context.track(true, true);
            }
        }
        ImGui::EndCombo();
    }
    if (!known) {
        // Said here rather than left for the playtest. An id the game does not
        // know spawns nothing, silently, minutes later.
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "'%s' is not one the game defines", value.c_str());
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
    ImGui::DragFloat3("size", &entity.trigger->size.x, 0.05f, 0.0f, 50.0f);
    track(context);
    stringField("event", entity.trigger->event, context);
}

// Whether the light moves, drawn inside the light's own section: it is a
// property of this light, not a second thing an author has to remember to add.
void drawLightAnimation(LightAuthor& light, InspectorContext& context)
{
    bool animated = light.animation.has_value();
    if (ImGui::Checkbox("animated", &animated)) {
        if (animated)
            light.animation = game::content::LightAnimAuthor{};
        else
            light.animation.reset();
        context.track(true, true);
    }
    if (!light.animation)
        return;

    game::content::LightAnimAuthor& animation = *light.animation;
    int mode = int(animation.mode);
    if (ImGui::Combo("motion", &mode, "steady\0flicker\0pulse\0"))
        animation.mode = game::content::LightAnimAuthor::Mode(mode);
    track(context);
    ImGui::DragFloat("rate", &animation.speed, 0.05f, 0.0f, 30.0f, "%.2f /s");
    track(context);
    ImGui::DragFloat("depth", &animation.amount, 0.005f, 0.0f, 1.0f, "%.2f");
    track(context);
    ImGui::DragFloat("phase", &animation.phase, 0.05f, 0.0f, 10.0f, "%.2f");
    track(context);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("give each torch a different phase, or a row of them "
                          "flickers in lockstep");
    ImGui::TextDisabled("modulation only darkens -- the colour above is the "
                        "brightest it gets");
}

// The lens, and nothing about where the camera is: position and facing are the
// entity's transform, edited with the gizmo like everything else.
void drawCamera(Entity& entity, InspectorContext& context)
{
    CameraAuthor& camera = *entity.camera;
    ImGui::DragFloat("fov", &camera.fovDegrees, 0.25f, 10.0f, 140.0f, "%.1f deg");
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
    if (ImGui::BeginCombo("framing", "pick a framing")) {
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

    ImGui::DragFloat("near", &camera.nearClip, 0.005f, 0.01f, 5.0f, "%.3f");
    track(context);
    ImGui::DragFloat("far", &camera.farClip, 1.0f, 1.0f, 5000.0f, "%.0f");
    track(context);
    if (camera.farClip <= camera.nearClip)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "far must be beyond near, or nothing draws");

    if (ImGui::Checkbox("active", &camera.active))
        context.track(true, true);
    ImGui::SameLine();
    ImGui::TextDisabled("(uncheck to park this framing)");
    ImGui::DragInt("priority", &camera.priority, 0.1f, -100, 100);
    track(context);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("the highest active camera in the scene is the one "
                          "the game looks through");
}

void drawSpin(Entity& entity, InspectorContext& context)
{
    SpinAuthor& spin = *entity.spin;
    ImGui::DragFloat("deg/s", &spin.degreesPerSecond, 1.0f, -720.0f, 720.0f,
                     "%.0f");
    track(context);
    ImGui::DragFloat3("axis", &spin.axis.x, 0.02f, -1.0f, 1.0f);
    track(context);
    if (glm::length(spin.axis) <= 0.0f)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "a zero axis spins nothing");
    ImGui::TextDisabled("children spin with it -- park a camera under this "
                        "for an orbit");
}

void drawShader(Entity& entity, InspectorContext& context)
{
    game::content::ShaderAuthor& sh = *entity.shader;
    ImGui::ColorEdit3("tint", &sh.tint.x);
    track(context);
    ImGui::SliderFloat("opacity", &sh.opacity, 0.0f, 1.0f, "%.2f");
    track(context);
    ImGui::Separator();
    ImGui::ColorEdit3("rim", &sh.rimColour.x);
    track(context);
    ImGui::SliderFloat("rim strength", &sh.rimStrength, 0.0f, 4.0f, "%.2f");
    track(context);
    ImGui::SliderFloat("rim power", &sh.rimPower, 0.25f, 16.0f, "%.2f");
    track(context);
    ImGui::Separator();
    ImGui::SliderFloat("cutout", &sh.alphaScissor, 0.0f, 1.0f, "%.2f");
    track(context);

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
    if (context.particleEffects && !context.particleEffects->empty()) {
        const std::string current = fx.effect.empty() ? "(none)" : fx.effect;
        if (ImGui::BeginCombo("effect", current.c_str())) {
            if (ImGui::Selectable("(none)", fx.effect.empty())) {
                fx.effect.clear();
                context.closed = true;
            }
            for (const std::string& name : *context.particleEffects) {
                if (ImGui::Selectable(name.c_str(), name == fx.effect)) {
                    fx.effect = name;
                    context.closed = true;
                }
            }
            ImGui::EndCombo();
        }
    } else {
        stringField("effect", fx.effect, context);
        ImGui::TextDisabled("no effect library loaded");
    }
    ImGui::DragFloat3("offset", &fx.offset.x, 0.02f);
    track(context);
    ImGui::SliderFloat("scale", &fx.scale, 0.05f, 8.0f, "%.2f");
    track(context);
    ImGui::Checkbox("playing", &fx.playing);
    track(context);
}

void drawOrbit(Entity& entity, InspectorContext& context)
{
    OrbitAuthor& orbit = *entity.orbit;
    ImGui::DragFloat3("centre", &orbit.centre.x, 0.05f);
    track(context);
    if (ImGui::Button("Centre on the origin")) {
        orbit.centre = glm::vec3(0.0f);
        context.track(true, true);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(in this entity's own frame)");

    ImGui::DragFloat("radius", &orbit.radius, 0.05f, 0.0f, 200.0f, "%.2f m");
    track(context);
    ImGui::DragFloat("deg/s", &orbit.degreesPerSecond, 1.0f, -720.0f, 720.0f,
                     "%.0f");
    track(context);
    // A rate an author can judge: "one turn every twelve seconds" is a decision
    // about pacing, where "30 deg/s" is arithmetic.
    if (orbit.degreesPerSecond != 0.0f) {
        ImGui::TextDisabled("one lap every %.1f s",
                            double(360.0f / std::abs(orbit.degreesPerSecond)));
    }
    ImGui::DragFloat("height", &orbit.height, 0.05f, -50.0f, 50.0f, "%.2f m");
    track(context);
    ImGui::DragFloat("phase", &orbit.phaseDegrees, 1.0f, 0.0f, 360.0f, "%.0f");
    track(context);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("where on the ring it starts -- two things on one "
                          "ring with no phase sit on top of each other");

    ImGui::Separator();
    ImGui::DragFloat3("axis", &orbit.axis.x, 0.02f, -1.0f, 1.0f);
    track(context);
    if (glm::length(orbit.axis) <= 0.0f)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "a zero axis is not a ring");

    int facing = int(orbit.facing);
    if (ImGui::Combo("facing", &facing, "free\0look at centre\0look along travel\0"))
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
    {"mesh", drawMesh},
    {"cell", drawCell},
    {"collider", drawCollider},
    {"light", drawLight},
    {"camera", drawCamera},
    {"spin", drawSpin},
    {"orbit", drawOrbit},
    {"shader", drawShader},
    {"particles", drawParticles},
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
    ImGui::SeparatorText(entity.parent.empty() ? "transform"
                                               : "transform (local to parent)");
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
