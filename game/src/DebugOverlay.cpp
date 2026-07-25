#include "DebugOverlay.h"

#include "CombatConfig.h"
#include "FpsController.h"
#include "GameDiagnostics.h"
#include "combat/FeelComponents.h"

#include <eng/Renderer.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace game {

namespace {

constexpr float kPanelWidth = 360.0f;

const char* const kPresetNames[] = {"ps1",  "ps2",      "gamecube",
                                    "n64",  "pixel-3d", "modern-ps1"};

bool section(const char* label)
{
    return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
}

// Small coloured swatch + label, for the collider shape legend.
void legend(const glm::vec3& c, const char* label)
{
    ImGui::ColorButton(label, ImVec4(c.r, c.g, c.b, 1.0f),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                       ImVec2(14, 14));
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
}

} // namespace

// ---------------------------------------------------------------- shell -----

void DebugOverlay::draw(const Deps& d)
{
    if (!mVisible)
        return;

    if (!mProfileInit) { // seed the editable profile cache from the combo default
        mRp = eng::renderPresetValues(mPreset + 1);
        mProfileInit = true;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - kPanelWidth,
                                   vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, vp->WorkSize.y));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("Debug Console", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("F1 console  -  F3 colliders  -  edits apply live");
    ImGui::Separator();

    if (ImGui::BeginTabBar("##dbgtabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Render"))    { drawRenderTab(d);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Shaders"))   { drawShadersTab(d);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Colliders")) { drawCollidersTab(d); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Combat"))    { drawCombatTab(d);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Feel"))      { drawFeelTab(d);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Player"))    { drawPlayerTab(d);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Materials")) { drawMaterialsTab(d); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void DebugOverlay::loadProfile(const Deps& d, int id)
{
    mRp = eng::renderPresetValues(id);
    if (d.renderer)
        eng::applyRenderPreset(*d.renderer, mRp);
    mProfileInit = true;
}

// ---------------------------------------------------------------- Render ----
// Core framebuffer / lighting / bloom / grade / camera. The full stylize shader
// set lives in the Shaders tab; together they cover every field of the render
// profile so a look can be tuned live and exported reproducibly.

void DebugOverlay::drawRenderTab(const Deps& d)
{
    eng::Renderer* r = d.renderer;
    if (!r) { ImGui::TextDisabled("Renderer unavailable."); return; }
    eng::RenderPresetValues& v = mRp;

    if (section("Render Profile")) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##profile", &mPreset, kPresetNames,
                         IM_ARRAYSIZE(kPresetNames)))
            loadProfile(d, mPreset + 1);
        if (ImGui::Button("Re-apply", ImVec2(-FLT_MIN, 0)))
            eng::applyRenderPreset(*r, v);
        ImGui::TextDisabled("Edit below; Copy as TOML in Shaders tab.");
    }

    if (section("Pixelation")) {
        if (ImGui::SliderInt("Pixel size", &v.pixelSize, 1, 16))
            r->setPixelSize(v.pixelSize);
        if (ImGui::Checkbox("Per-pixel lighting", &v.perPixel))
            r->setPerPixelLightingEnabled(v.perPixel);
        if (ImGui::SliderFloat("Precision mult", &v.precisionMultiplier, 0.25f, 4.0f))
            r->setGlobalMaterialParam("precisionMultiplier", v.precisionMultiplier);
        if (ImGui::SliderFloat("Affine warp", &v.affineAmount, 0.0f, 1.0f))
            r->setGlobalMaterialParam("affineAmount", v.affineAmount);
    }

    if (section("Lighting")) {
        bool ch = ImGui::Checkbox("Banded", &v.bandedLightingEnabled);
        ch |= ImGui::SliderFloat("Bands", &v.bandedLightSteps, 0.0f, 8.0f, "%.0f");
        if (ch)
            r->setLightSteps(v.bandedLightingEnabled ? v.bandedLightSteps : 0.0f);
        if (ImGui::SliderFloat("Band softness", &v.stepSoftness, 0.0f, 1.0f))
            r->setLightStepSoftness(v.stepSoftness);
    }

    if (section("Bloom")) {
        if (ImGui::Checkbox("Enabled##bloom", &v.bloom))
            r->setBloomEnabled(v.bloom);
        bool ch = ImGui::SliderFloat("Threshold", &v.bloomThreshold, 0.0f, 2.0f);
        ch |= ImGui::SliderFloat("Intensity", &v.bloomIntensity, 0.0f, 2.0f);
        if (ch)
            r->setBloomParams(v.bloomThreshold, v.bloomIntensity);
    }

    if (section("Colour Grade")) {
        bool g = ImGui::SliderFloat("Desaturate", &v.gradeDesaturate, 0.0f, 1.0f);
        g |= ImGui::SliderFloat("Contrast", &v.gradeContrast, 0.5f, 2.0f);
        g |= ImGui::ColorEdit3("Shadow tint", &v.gradeShadow.x);
        g |= ImGui::ColorEdit3("Mid tint", &v.gradeMid.x);
        if (g)
            r->setGradeParams(v.gradeDesaturate, v.gradeContrast, v.gradeShadow,
                              v.gradeMid);
        if (ImGui::SliderFloat("Saturation", &v.gradeSaturation, 0.0f, 2.0f))
            r->setMaterialParam("PSX/DitherPost", "gradeSaturation", v.gradeSaturation);
        if (ImGui::SliderFloat("Tint strength", &v.gradeTintStrength, 0.0f, 1.0f))
            r->setMaterialParam("PSX/DitherPost", "gradeTintStrength", v.gradeTintStrength);
        if (ImGui::SliderFloat("Black lift", &v.gradeBlackLift, 0.0f, 0.5f))
            r->setMaterialParam("PSX/DitherPost", "gradeBlackLift", v.gradeBlackLift);
    }

    if (section("Quantize / Dither")) {
        if (ImGui::SliderFloat("Colour depth", &v.colDepth, 3.0f, 63.0f, "%.0f"))
            r->setMaterialParam("PSX/DitherPost", "colDepth", v.colDepth);
        if (ImGui::SliderFloat("Dither banding", &v.ditherBanding, 0.0f, 0.1f, "%.4f"))
            r->setMaterialParam("PSX/DitherPost", "ditherBanding", v.ditherBanding);
        if (ImGui::SliderFloat("Dither dark fade", &v.ditherDarkFade, 0.0f, 1.0f))
            r->setMaterialParam("PSX/DitherPost", "ditherDarkFade", v.ditherDarkFade);
    }

    if (section("Vignette")) {
        bool ch = ImGui::Checkbox("Enabled##vig", &v.vignetteEnabled);
        ch |= ImGui::SliderFloat("Strength##vig", &v.vignetteStrength, 0.0f, 1.0f);
        if (ch)
            r->setMaterialParam("PSX/DitherPost", "vignetteStrength",
                                v.vignetteEnabled ? v.vignetteStrength : 0.0f);
        if (ImGui::ColorEdit3("Colour##vig", &v.vignetteColor.x))
            r->setMaterialParam("PSX/DitherPost", "vignetteColor", v.vignetteColor);
    }

    // Environment lives outside the render profile (it comes from the scene
    // palette), so it reads/writes the live EnvState directly.
    if (section("Environment")) {
        const eng::EnvState& e = r->envState();
        glm::vec3 amb = e.ambient;
        if (ImGui::ColorEdit3("Ambient", &amb.x)) r->setAmbient(amb);
        glm::vec3 fog = e.fogColour; float fogD = e.fogDensity;
        bool ch = ImGui::ColorEdit3("Fog colour", &fog.x);
        ch |= ImGui::SliderFloat("Fog density", &fogD, 0.0f, 0.3f, "%.4f");
        if (ch) r->setFog(fog, fogD);
        glm::vec3 bg = e.background;
        if (ImGui::ColorEdit3("Background", &bg.x)) r->setBackground(bg);
    }

    if (section("Camera")) {
        const eng::EnvState& e = r->envState();
        float nearC = e.nearClip, farC = e.farClip;
        bool ch = ImGui::SliderFloat("Near", &nearC, 0.01f, 1.0f, "%.3f");
        ch |= ImGui::SliderFloat("Far", &farC, 100.0f, 8000.0f, "%.0f");
        if (ch) r->setCameraClip(nearC, farC);
        bool wire = e.wireframe;
        if (ImGui::Checkbox("Wireframe (mesh)", &wire)) r->setWireframeDebug(wire);
        ImGui::TextDisabled("FOV is on the Player tab (controller-owned).");
    }
}

// --------------------------------------------------------------- Shaders ----
// Every PSX stylize-shader parameter, so a render profile is fully reproducible.

void DebugOverlay::drawShadersTab(const Deps& d)
{
    eng::Renderer* r = d.renderer;
    if (!r) { ImGui::TextDisabled("Renderer unavailable."); return; }
    eng::RenderPresetValues& v = mRp;
    const char* kStylize = "PSX/PixelStylize";

    if (section("Master")) {
        if (ImGui::Checkbox("Stylize enabled", &v.stylizeEnabled))
            r->setMaterialParam(kStylize, "stylizeEnabled", v.stylizeEnabled ? 1.0f : 0.0f);
    }

    if (section("Ink / Shadow")) {
        if (ImGui::Checkbox("Enabled##ink", &v.inkEnabled))
            r->setMaterialParam(kStylize, "shadowsEnabled", v.inkEnabled ? 1.0f : 0.0f);
        if (ImGui::SliderFloat("Strength##ink", &v.inkStrength, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "shadowStrength", v.inkStrength);
        if (ImGui::SliderFloat("Threshold##ink", &v.inkThreshold, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "shadowThreshold", v.inkThreshold);
        if (ImGui::ColorEdit3("Colour##ink", &v.inkColor.x))
            r->setMaterialParam(kStylize, "shadowColor", v.inkColor);
    }

    if (section("Highlights")) {
        if (ImGui::Checkbox("Enabled##hl", &v.highlightsEnabled))
            r->setMaterialParam(kStylize, "highlightsEnabled", v.highlightsEnabled ? 1.0f : 0.0f);
        if (ImGui::SliderFloat("Strength##hl", &v.highlightStrength, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "highlightStrength", v.highlightStrength);
        if (ImGui::SliderFloat("Threshold##hl", &v.highlightThreshold, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "highlightThreshold", v.highlightThreshold);
        if (ImGui::SliderFloat("Dark fade##hl", &v.highlightDarkFade, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "highlightDarkFade", v.highlightDarkFade);
        if (ImGui::ColorEdit3("Colour##hl", &v.highlightColor.x))
            r->setMaterialParam(kStylize, "highlightColor", v.highlightColor);
    }

    if (section("Outlines")) {
        if (ImGui::Checkbox("Enabled##ol", &v.outlinesEnabled))
            r->setMaterialParam(kStylize, "outlineEnabled", v.outlinesEnabled ? 1.0f : 0.0f);
        if (ImGui::SliderFloat("Opacity##ol", &v.outlineOpacity, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "outlineOpacity", v.outlineOpacity);
        if (ImGui::SliderFloat("Thickness##ol", &v.outlineThickness, 0.5f, 4.0f))
            r->setMaterialParam(kStylize, "outlineThickness", v.outlineThickness);
        if (ImGui::SliderFloat("Depth sens##ol", &v.outlineDepthSens, 0.0f, 32.0f))
            r->setMaterialParam(kStylize, "outlineDepthSens", v.outlineDepthSens);
        if (ImGui::SliderFloat("Normal sens##ol", &v.outlineNormalSens, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "outlineNormalSens", v.outlineNormalSens);
        if (ImGui::SliderFloat("Sharpness##ol", &v.outlineSharpness, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "outlineSharpness", v.outlineSharpness);
        if (ImGui::SliderFloat("Dist fade##ol", &v.outlineDistFade, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "outlineDistFade", v.outlineDistFade);
        if (ImGui::SliderFloat("Dark fade##ol", &v.outlineDarkFade, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "outlineDarkFade", v.outlineDarkFade);
        if (ImGui::ColorEdit3("Colour##ol", &v.outlineColor.x))
            r->setMaterialParam(kStylize, "outlineColor", v.outlineColor);
    }

    if (section("Hardware Resolve")) {
        if (ImGui::SliderFloat("Mode", &v.hardwareResolveMode, 0.0f, 8.0f, "%.0f"))
            r->setMaterialParam("PSX/HardwareResolve", "resolveMode", v.hardwareResolveMode);
        if (ImGui::SliderFloat("Strength##hr", &v.hardwareResolveStrength, 0.0f, 1.0f))
            r->setMaterialParam("PSX/HardwareResolve", "resolveStrength", v.hardwareResolveStrength);
    }

    ImGui::Separator();
    if (ImGui::Button("Copy profile as TOML", ImVec2(-FLT_MIN, 0))) {
        char buf[2048];
        std::snprintf(buf, sizeof(buf),
            "[render_profile]\n"
            "pixel_size = %d\nprecision_multiplier = %.4f\naffine_amount = %.4f\n"
            "per_pixel = %s\nbanded_lighting = %s\nband_light_steps = %.3f\nstep_softness = %.4f\n"
            "bloom = %s\nbloom_threshold = %.4f\nbloom_intensity = %.4f\n"
            "grade_desaturate = %.4f\ngrade_contrast = %.4f\ngrade_saturation = %.4f\n"
            "grade_tint_strength = %.4f\ngrade_black_lift = %.4f\n"
            "grade_shadow = [%.4f, %.4f, %.4f]\ngrade_mid = [%.4f, %.4f, %.4f]\n"
            "col_depth = %.3f\ndither_banding = %.4f\ndither_dark_fade = %.4f\n"
            "vignette = %s\nvignette_strength = %.4f\nvignette_color = [%.4f, %.4f, %.4f]\n"
            "stylize = %s\nink = %s\nink_strength = %.4f\nink_threshold = %.4f\nink_color = [%.4f, %.4f, %.4f]\n"
            "highlights = %s\nhighlight_strength = %.4f\nhighlight_threshold = %.4f\nhighlight_dark_fade = %.4f\nhighlight_color = [%.4f, %.4f, %.4f]\n"
            "outlines = %s\noutline_opacity = %.4f\noutline_thickness = %.4f\noutline_depth_sens = %.4f\noutline_normal_sens = %.4f\noutline_sharpness = %.4f\noutline_dist_fade = %.4f\noutline_dark_fade = %.4f\noutline_color = [%.4f, %.4f, %.4f]\n"
            "hw_resolve_mode = %.3f\nhw_resolve_strength = %.4f\n",
            v.pixelSize, v.precisionMultiplier, v.affineAmount,
            v.perPixel?"true":"false", v.bandedLightingEnabled?"true":"false", v.bandedLightSteps, v.stepSoftness,
            v.bloom?"true":"false", v.bloomThreshold, v.bloomIntensity,
            v.gradeDesaturate, v.gradeContrast, v.gradeSaturation, v.gradeTintStrength, v.gradeBlackLift,
            v.gradeShadow.x, v.gradeShadow.y, v.gradeShadow.z, v.gradeMid.x, v.gradeMid.y, v.gradeMid.z,
            v.colDepth, v.ditherBanding, v.ditherDarkFade,
            v.vignetteEnabled?"true":"false", v.vignetteStrength, v.vignetteColor.x, v.vignetteColor.y, v.vignetteColor.z,
            v.stylizeEnabled?"true":"false", v.inkEnabled?"true":"false", v.inkStrength, v.inkThreshold, v.inkColor.x, v.inkColor.y, v.inkColor.z,
            v.highlightsEnabled?"true":"false", v.highlightStrength, v.highlightThreshold, v.highlightDarkFade, v.highlightColor.x, v.highlightColor.y, v.highlightColor.z,
            v.outlinesEnabled?"true":"false", v.outlineOpacity, v.outlineThickness, v.outlineDepthSens, v.outlineNormalSens, v.outlineSharpness, v.outlineDistFade, v.outlineDarkFade, v.outlineColor.x, v.outlineColor.y, v.outlineColor.z,
            v.hardwareResolveMode, v.hardwareResolveStrength);
        ImGui::SetClipboardText(buf);
    }
    ImGui::TextDisabled("Copies the live profile to the clipboard.");
}

// -------------------------------------------------------------- Colliders ---

void DebugOverlay::drawCollidersTab(const Deps& d)
{
    ColliderDebug* c = d.colliders;
    if (!c) { ImGui::TextDisabled("Collider view unavailable."); return; }

    ImGui::Checkbox("Show colliders (F3)", &c->enabled);
    ImGui::Separator();

    const char* modes[] = {"By shape", "By layer", "Uniform"};
    ImGui::Combo("Colour mode", &c->colorMode, modes, IM_ARRAYSIZE(modes));
    if (c->colorMode == 2)
        ImGui::ColorEdit3("Uniform colour", &c->uniformColor.x);
    ImGui::SliderFloat("Brightness", &c->brightness, 0.5f, 4.0f);
    ImGui::SliderFloat("Line thickness", &c->thickness, 1.0f, 4.0f);
    ImGui::Checkbox("Include static level", &c->includeStatic);
    ImGui::TextDisabled("Full-res overlay: crisp, ignores pixelation.");

    ImGui::Separator();
    if (c->colorMode == 0) {
        ImGui::TextDisabled("Shape legend");
        legend({0.30f, 1.00f, 0.45f}, "Box");
        legend({0.30f, 0.85f, 1.00f}, "Sphere");
        legend({1.00f, 0.90f, 0.30f}, "Capsule");
        legend({1.00f, 0.60f, 0.20f}, "Cylinder");
        legend({1.00f, 0.30f, 0.90f}, "Mesh / hull");
    } else if (c->colorMode == 1) {
        ImGui::TextDisabled("Layer legend");
        legend({0.5f, 0.5f, 0.5f}, "Static");
        legend({0.2f, 1.0f, 0.2f}, "Prop");
        legend({1.0f, 1.0f, 0.2f}, "Projectile");
        legend({0.2f, 0.8f, 1.0f}, "Player");
        legend({1.0f, 0.4f, 1.0f}, "Trigger");
    }
}

// ---------------------------------------------------------------- Combat ----

void DebugOverlay::drawCombatTab(const Deps& d)
{
    CombatConfig* c = d.combat;
    if (!c) { ImGui::TextDisabled("Combat config unavailable."); return; }

    if (section("Fireball")) {
        ImGui::PushID("fb");
        ImGui::SliderFloat("Speed", &c->fireball.speed, 1.0f, 60.0f);
        ImGui::SliderFloat("Radius", &c->fireball.radius, 0.02f, 1.0f);
        ImGui::SliderFloat("Mass", &c->fireball.mass, 0.05f, 5.0f);
        ImGui::SliderFloat("TTL", &c->fireball.ttl, 0.5f, 15.0f);
        ImGui::SliderFloat("Impact impulse", &c->fireball.impactImpulse, 0.0f, 20.0f);
        ImGui::ColorEdit3("Light colour", &c->fireball.lightColour.x);
        ImGui::SliderFloat("Light range", &c->fireball.lightRange, 0.5f, 12.0f);
        ImGui::PopID();
    }
    if (section("Beam")) {
        ImGui::PushID("bm");
        ImGui::SliderFloat("Range", &c->beam.range, 1.0f, 80.0f);
        ImGui::SliderFloat("Width", &c->beam.width, 0.01f, 0.5f);
        ImGui::SliderFloat("Impulse", &c->beam.impulse, 0.0f, 20.0f);
        ImGui::SliderFloat("Segment TTL", &c->beam.segmentTtl, 0.02f, 0.5f);
        ImGui::ColorEdit3("Light colour", &c->beam.lightColour.x);
        ImGui::SliderFloat("Light range", &c->beam.lightRange, 0.5f, 12.0f);
        ImGui::PopID();
    }
    if (section("Arrow")) {
        ImGui::PushID("ar");
        ImGui::SliderFloat("Speed", &c->arrow.speed, 5.0f, 120.0f);
        ImGui::SliderFloat("Radius", &c->arrow.radius, 0.01f, 0.2f);
        ImGui::SliderFloat("Half height", &c->arrow.halfHeight, 0.05f, 0.6f);
        ImGui::SliderFloat("Mass", &c->arrow.mass, 0.02f, 2.0f);
        ImGui::SliderFloat("TTL", &c->arrow.ttl, 1.0f, 20.0f);
        ImGui::PopID();
    }
    if (section("Melee")) {
        ImGui::PushID("ml");
        ImGui::SliderFloat("Reach", &c->melee.reach, 0.5f, 4.0f);
        ImGui::SliderFloat("Radius", &c->melee.radius, 0.1f, 1.5f);
        ImGui::SliderFloat("Impulse", &c->melee.impulse, 0.0f, 20.0f);
        ImGui::SliderFloat("Windup", &c->melee.windup, 0.0f, 0.5f);
        ImGui::SliderFloat("Active", &c->melee.active, 0.0f, 0.5f);
        ImGui::PopID();
    }
}

// ------------------------------------------------------------------ Feel ----

void DebugOverlay::drawFeelTab(const Deps& d)
{
    entt::registry* reg = d.registry;
    if (!reg || d.player == entt::null || !reg->valid(d.player)) {
        ImGui::TextDisabled("Player entity unavailable.");
        return;
    }
    const entt::entity p = d.player;

    if (auto* st = reg->try_get<Stamina>(p); st && section("Stamina")) {
        ImGui::PushID("st");
        ImGui::ProgressBar(st->max > 0 ? st->current / st->max : 0.0f, ImVec2(-FLT_MIN, 0));
        ImGui::SliderFloat("Max", &st->max, 10.0f, 300.0f);
        ImGui::SliderFloat("Regen rate", &st->regenRate, 0.0f, 100.0f);
        ImGui::SliderFloat("Regen delay", &st->regenDelay, 0.0f, 3.0f);
        if (ImGui::Button("Refill")) st->current = st->max;
        ImGui::PopID();
    }
    if (auto* po = reg->try_get<Poise>(p); po && section("Poise")) {
        ImGui::PushID("po");
        ImGui::ProgressBar(po->max > 0 ? po->current / po->max : 0.0f, ImVec2(-FLT_MIN, 0));
        ImGui::SliderFloat("Max", &po->max, 10.0f, 300.0f);
        ImGui::SliderFloat("Regen rate", &po->regenRate, 0.0f, 100.0f);
        ImGui::SliderFloat("Regen delay", &po->regenDelay, 0.0f, 3.0f);
        ImGui::Text("Stagger immunity: %.2fs", po->staggerImmuneFor);
        ImGui::PopID();
    }
    if (auto* mn = reg->try_get<Mana>(p); mn && section("Mana")) {
        ImGui::PushID("mn");
        ImGui::ProgressBar(mn->max > 0 ? mn->current / mn->max : 0.0f, ImVec2(-FLT_MIN, 0));
        ImGui::SliderFloat("Max", &mn->max, 10.0f, 300.0f);
        ImGui::SliderFloat("Regen rate", &mn->regenRate, 0.0f, 50.0f);
        if (ImGui::Button("Refill")) mn->current = mn->max;
        ImGui::PopID();
    }
    if (auto* as = reg->try_get<ActionState>(p); as && section("Action State")) {
        static const char* kPhase[] = {"Idle","Windup","Active","Recovery",
                                       "Deflect","Dodge","Staggered"};
        const int i = int(as->phase);
        ImGui::Text("Phase: %s", (i >= 0 && i < 7) ? kPhase[i] : "?");
        ImGui::Text("Timer: %.3fs", as->timer);
        ImGui::Separator();
        ImGui::SliderFloat("Windup", &as->attack.windup, 0.0f, 1.0f);
        ImGui::SliderFloat("Active", &as->attack.active, 0.0f, 0.5f);
        ImGui::SliderFloat("Recovery", &as->attack.recovery, 0.0f, 1.0f);
        ImGui::SliderFloat("Stamina cost", &as->attack.staminaCost, 0.0f, 60.0f);
        ImGui::SliderFloat("Poise damage", &as->attack.poiseDamage, 0.0f, 80.0f);
    }
}

// ---------------------------------------------------------- Player + Stats --

void DebugOverlay::drawPlayerTab(const Deps& d)
{
    if (section("Frame Stats") && d.prof) {
        const ProfHud& pr = *d.prof;
        float sum = 0.0f, peak = 0.0f;
        for (float m : pr.frameHist) { sum += m; peak = std::max(peak, m); }
        const float avg = sum / float(ProfHud::kHist);
        ImGui::Text("Frame %.2f ms  (%.0f fps)", avg, avg > 0.0f ? 1000.0f / avg : 0.0f);
        ImGui::PlotLines("##ft", pr.frameHist, ProfHud::kHist, pr.histHead, nullptr,
                         0.0f, std::max(16.7f, peak * 1.1f), ImVec2(-FLT_MIN, 60));
        ImGui::Separator();
        for (int i = 0; i < ProfHud::kCount; ++i)
            ImGui::Text("%-9s %6.2f ms", ProfHud::kNames[i], pr.ms[i]);
    }

    FpsController* f = d.fps;
    if (!f) { ImGui::TextDisabled("FPS controller unavailable."); return; }
    if (section("Movement & Camera")) {
        ImGui::SliderFloat("Move speed", &f->speed(), 0.5f, 12.0f);
        ImGui::SliderFloat("Mouse sens", &f->sensitivity(), 0.0005f, 0.01f, "%.4f");
        float fov = f->baseFov();
        if (ImGui::SliderFloat("Base FOV", &fov, 40.0f, 110.0f, "%.0f"))
            f->setBaseFov(fov);
        // Near/far clip: read the live values off the renderer, write back on
        // change. Tight near planes reduce z-fighting; far drives cull distance.
        if (d.renderer) {
            const eng::EnvState& env = d.renderer->envState();
            float nearC = env.nearClip, farC = env.farClip;
            bool clipChanged = false;
            clipChanged |= ImGui::SliderFloat("Near clip", &nearC, 0.01f, 1.0f, "%.3f");
            clipChanged |= ImGui::SliderFloat("Far clip", &farC, 10.0f, 4000.0f, "%.0f");
            if (clipChanged)
                d.renderer->setCameraClip(nearC, farC);
        }
        ImGui::SeparatorText("Feel");
        ImGui::SliderFloat("Sprint FOV kick", &f->sprintFovKick(), 0.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("Head-bob amount", &f->bobAmount(), 0.0f, 0.1f, "%.3f");
        ImGui::SliderFloat("Head-bob speed", &f->bobSpeed(), 0.0f, 20.0f, "%.1f");
        ImGui::Separator();
        ImGui::Text("Horizontal speed: %.2f m/s", f->horizontalSpeed());
        ImGui::ProgressBar(f->sprintStamina(), ImVec2(-FLT_MIN, 0), "sprint stamina");
    }
}

// -------------------------------------------------------------- Materials ---

void DebugOverlay::drawMaterialsTab(const Deps& d)
{
    eng::Renderer* r = d.renderer;
    if (!r) { ImGui::TextDisabled("Renderer unavailable."); return; }

    if (section("Custom Global Param")) {
        ImGui::InputText("Param", mParamName, sizeof(mParamName));
        ImGui::SliderFloat("Value", &mParamValue, -2.0f, 4.0f);
        if (ImGui::Button("Set global", ImVec2(-FLT_MIN, 0)))
            r->setGlobalMaterialParam(mParamName, mParamValue);
        ImGui::TextDisabled("Sets a float on every material declaring it.");
    }

    if (section("Loaded Materials")) {
        const std::vector<std::string> names = r->materialNames();
        ImGui::TextDisabled("%zu materials loaded", names.size());
        if (ImGui::BeginListBox("##mats", ImVec2(-FLT_MIN, 240))) {
            for (int i = 0; i < int(names.size()); ++i)
                if (ImGui::Selectable(names[size_t(i)].c_str(), i == mMaterialIdx))
                    mMaterialIdx = i;
            ImGui::EndListBox();
        }
    }
}

// -------------------------------------------------------------- PerfOverlay -

void PerfOverlay::draw(const ProfHud* prof, eng::Renderer* renderer)
{
    if (!mVisible || !prof)
        return;

    // Frame-time stats over the rolling history ring.
    float sum = 0.0f, peak = 0.0f, low = 1e9f;
    for (float m : prof->frameHist) {
        sum += m;
        peak = std::max(peak, m);
        if (m > 0.0f) low = std::min(low, m);
    }
    const float avg = sum / float(ProfHud::kHist);
    const float fps = avg > 0.0f ? 1000.0f / avg : 0.0f;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 8, vp->WorkPos.y + 8),
                            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin("##perf", nullptr, flags)) {
        ImGui::End();
        return;
    }

    // Colour the FPS by health (green >=58, yellow >=30, red below).
    const ImVec4 col = fps >= 58.0f ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                     : fps >= 30.0f ? ImVec4(1.0f, 0.9f, 0.3f, 1.0f)
                                    : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
    ImGui::TextColored(col, "%.0f FPS", fps);
    ImGui::SameLine();
    ImGui::Text("  %.2f ms  (min %.2f / max %.2f)", avg, low >= 1e8f ? 0.0f : low,
                peak);

    ImGui::PlotLines("##perfft", prof->frameHist, ProfHud::kHist, prof->histHead,
                     nullptr, 0.0f, std::max(33.3f, peak * 1.1f), ImVec2(220, 40));

    ImGui::Separator();
    // CPU phase breakdown as labelled bars (fraction of the largest phase).
    float phaseMax = 1e-3f;
    for (int i = 0; i < ProfHud::kCount; ++i)
        phaseMax = std::max(phaseMax, prof->ms[i]);
    for (int i = 0; i < ProfHud::kCount; ++i) {
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "%.2f ms", prof->ms[i]);
        ImGui::Text("%-8s", ProfHud::kNames[i]);
        ImGui::SameLine(72);
        ImGui::ProgressBar(prof->ms[i] / phaseMax, ImVec2(120, 12), lbl);
    }

    if (renderer) {
        ImGui::Separator();
        size_t batches = 0, tris = 0;
        renderer->frameStats(batches, tris);
        ImGui::Text("Draw calls: %zu", batches);
        ImGui::Text("Triangles : %s",
                    [&] {
                        static char b[32];
                        if (tris >= 1000000)
                            std::snprintf(b, sizeof(b), "%.2fM", double(tris) / 1e6);
                        else if (tris >= 1000)
                            std::snprintf(b, sizeof(b), "%.1fk", double(tris) / 1e3);
                        else
                            std::snprintf(b, sizeof(b), "%zu", tris);
                        return b;
                    }());
    }
    ImGui::Separator();
    ImGui::TextDisabled("F4 hide  -  F1 console");
    ImGui::End();
}

} // namespace game
