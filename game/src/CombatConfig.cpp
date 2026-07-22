#include "CombatConfig.h"

#include <eng/Input.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <imgui.h>

#include <array>
#include <cstring>

// ---- toml load -------------------------------------------------------------

namespace {

float num(const toml::table& t, const char* key, float fallback) {
    return float(t[key].value_or(double(fallback)));
}

std::string str(const toml::table& t, const char* key, const std::string& fallback) {
    return t[key].value_or(fallback);
}

glm::vec3 col(const toml::table& t, const char* key, glm::vec3 fallback) {
    const toml::array* a = t[key].as_array();
    if (!a || a->size() != 3) return fallback;
    return glm::vec3(float((*a)[0].value_or(double(fallback.x))),
                     float((*a)[1].value_or(double(fallback.y))),
                     float((*a)[2].value_or(double(fallback.z))));
}

// Look up an action's first key name in the [bindings] table (string or array).
std::string binding(const toml::table& root, const std::string& action,
                    const std::string& fallback) {
    const toml::table* b = root["bindings"].as_table();
    if (!b) return fallback;
    if (const toml::node* n = b->get(action)) {
        if (auto s = n->value<std::string>()) return *s;
        if (const toml::array* a = n->as_array(); a && a->size() > 0)
            return (*a)[0].value_or(fallback);
    }
    return fallback;
}

} // namespace

bool CombatConfig::load(const std::string& tomlPath) {
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) return false;
    const toml::table& root = parsed.table();

    if (const toml::table* c = root["combat"]["fireball"].as_table()) {
        fireball.speed         = num(*c, "speed", fireball.speed);
        fireball.radius        = num(*c, "radius", fireball.radius);
        fireball.mass          = num(*c, "mass", fireball.mass);
        fireball.ttl           = num(*c, "ttl", fireball.ttl);
        fireball.impactImpulse = num(*c, "impact_impulse", fireball.impactImpulse);
        fireball.lightColour   = col(*c, "light_colour", fireball.lightColour);
        fireball.lightRange    = num(*c, "light_range", fireball.lightRange);
        fireball.trailParticle  = str(*c, "trail_particle", fireball.trailParticle);
        fireball.muzzleParticle = str(*c, "muzzle_particle", fireball.muzzleParticle);
        fireball.impactParticle = str(*c, "impact_particle", fireball.impactParticle);
    }
    if (const toml::table* c = root["combat"]["beam"].as_table()) {
        beam.range      = num(*c, "range", beam.range);
        beam.width      = num(*c, "width", beam.width);
        beam.impulse    = num(*c, "impulse", beam.impulse);
        beam.segmentTtl = num(*c, "segment_ttl", beam.segmentTtl);
        beam.lightColour = col(*c, "light_colour", beam.lightColour);
        beam.lightRange  = num(*c, "light_range", beam.lightRange);
        beam.coreParticle   = str(*c, "core_particle", beam.coreParticle);
        beam.impactParticle = str(*c, "impact_particle", beam.impactParticle);
    }
    if (const toml::table* c = root["combat"]["arrow"].as_table()) {
        arrow.speed      = num(*c, "speed", arrow.speed);
        arrow.radius     = num(*c, "radius", arrow.radius);
        arrow.halfHeight = num(*c, "half_height", arrow.halfHeight);
        arrow.mass       = num(*c, "mass", arrow.mass);
        arrow.ttl        = num(*c, "ttl", arrow.ttl);
    }
    if (const toml::table* c = root["combat"]["melee"].as_table()) {
        melee.reach   = num(*c, "reach", melee.reach);
        melee.radius  = num(*c, "radius", melee.radius);
        melee.impulse = num(*c, "impulse", melee.impulse);
        melee.windup  = num(*c, "windup", melee.windup);
        melee.active  = num(*c, "active", melee.active);
    }

    // Current hotkeys mirror [bindings] so the UI shows/edit the live key.
    fireball.key = binding(root, fireball.action, fireball.key);
    beam.key     = binding(root, beam.action, beam.key);
    arrow.key    = binding(root, arrow.action, arrow.key);
    return true;
}

// ---- debug UI --------------------------------------------------------------

namespace {

// One editable hotkey row: an InputText holding the SDL key name plus an Apply
// button that rebinds the action live.
void hotkeyRow(eng::Input& input, const char* label,
               const std::string& action, std::string& key) {
    std::array<char, 32> buf{};
    std::strncpy(buf.data(), key.c_str(), buf.size() - 1);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputText("##key", buf.data(), buf.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        key = buf.data();
        input.rebind(action, key);
    } else {
        key = buf.data();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply"))
        input.rebind(action, key);
    ImGui::SameLine();
    ImGui::Text("%s  (%s)", label, action.c_str());
    ImGui::PopID();
}

} // namespace

void CombatConfig::drawDebugUi(eng::Input& input) {
    if (ImGui::CollapsingHeader("Fireball", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("fireball");
        ImGui::SliderFloat("speed (m/s)", &fireball.speed, 2.0f, 60.0f);
        ImGui::SliderFloat("radius", &fireball.radius, 0.02f, 0.6f);
        ImGui::SliderFloat("mass", &fireball.mass, 0.05f, 5.0f);
        ImGui::SliderFloat("ttl (s)", &fireball.ttl, 0.5f, 15.0f);
        ImGui::SliderFloat("impact impulse", &fireball.impactImpulse, 0.0f, 30.0f);
        ImGui::ColorEdit3("light colour", &fireball.lightColour.x);
        ImGui::SliderFloat("light range", &fireball.lightRange, 0.0f, 12.0f);
        hotkeyRow(input, "cast", fireball.action, fireball.key);
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Beam", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("beam");
        ImGui::SliderFloat("range (m)", &beam.range, 2.0f, 80.0f);
        ImGui::SliderFloat("width", &beam.width, 0.01f, 0.4f);
        ImGui::SliderFloat("impulse", &beam.impulse, 0.0f, 30.0f);
        ImGui::SliderFloat("segment ttl", &beam.segmentTtl, 0.02f, 1.0f);
        ImGui::ColorEdit3("light colour", &beam.lightColour.x);
        ImGui::SliderFloat("light range", &beam.lightRange, 0.0f, 12.0f);
        hotkeyRow(input, "cast", beam.action, beam.key);
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Arrow")) {
        ImGui::PushID("arrow");
        ImGui::SliderFloat("speed (m/s)", &arrow.speed, 5.0f, 120.0f);
        ImGui::SliderFloat("radius", &arrow.radius, 0.01f, 0.2f);
        ImGui::SliderFloat("half height", &arrow.halfHeight, 0.05f, 0.6f);
        ImGui::SliderFloat("mass", &arrow.mass, 0.02f, 2.0f);
        ImGui::SliderFloat("ttl (s)", &arrow.ttl, 1.0f, 30.0f);
        hotkeyRow(input, "fire", arrow.action, arrow.key);
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Melee (sword)")) {
        ImGui::PushID("melee");
        ImGui::SliderFloat("reach (m)", &melee.reach, 0.5f, 4.0f);
        ImGui::SliderFloat("radius", &melee.radius, 0.1f, 1.5f);
        ImGui::SliderFloat("impulse", &melee.impulse, 0.0f, 30.0f);
        ImGui::SliderFloat("windup (s)", &melee.windup, 0.0f, 0.5f);
        ImGui::SliderFloat("active (s)", &melee.active, 0.02f, 0.5f);
        ImGui::TextDisabled("bound to Left Mouse");
        ImGui::PopID();
    }
}
