#include "RenderPalette.h"

#include <eng/Log.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

namespace {

float num(const toml::table& t, const char* key, float fallback) {
    return float(t[key].value_or(double(fallback)));
}

glm::vec3 vec3Of(const toml::table& t, const char* key, glm::vec3 fallback) {
    const toml::array* a = t[key].as_array();
    if (!a || a->size() != 3) return fallback;
    return glm::vec3(float((*a)[0].value_or(double(fallback.x))),
                     float((*a)[1].value_or(double(fallback.y))),
                     float((*a)[2].value_or(double(fallback.z))));
}

glm::vec4 vec4Of(const toml::table& t, const char* key, glm::vec4 fallback) {
    const toml::array* a = t[key].as_array();
    if (!a || a->size() != 4) return fallback;
    return glm::vec4(float((*a)[0].value_or(double(fallback.x))),
                     float((*a)[1].value_or(double(fallback.y))),
                     float((*a)[2].value_or(double(fallback.z))),
                     float((*a)[3].value_or(double(fallback.w))));
}

} // namespace

bool loadRenderPalette(const std::string& tomlPath, const std::string& name,
                       RenderPalette& out) {
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("RenderPalette: failed to parse %s", tomlPath.c_str());
        return false;
    }
    const toml::table* p = parsed.table()["palette"][name].as_table();
    if (!p) {
        eng::log::error("RenderPalette: [palette.%s] missing in %s",
                        name.c_str(), tomlPath.c_str());
        return false;
    }

    // Start from the current values so absent keys keep their defaults.
    RenderPalette r = out;
    const toml::table& t = *p;
    r.ambientSrgb       = vec3Of(t, "ambient_srgb", r.ambientSrgb);
    r.ambientScale      = num(t, "ambient_scale", r.ambientScale);
    r.sunYawDeg         = num(t, "sun_yaw_deg", r.sunYawDeg);
    r.sunPitchDeg       = num(t, "sun_pitch_deg", r.sunPitchDeg);
    r.sunColourSrgb     = vec3Of(t, "sun_colour_srgb", r.sunColourSrgb);
    r.sunScale          = num(t, "sun_scale", r.sunScale);
    r.fogSrgb           = vec3Of(t, "fog_srgb", r.fogSrgb);
    r.fogDensity        = num(t, "fog_density", r.fogDensity);
    r.backgroundSrgb    = vec3Of(t, "background_srgb", r.backgroundSrgb);
    r.lightSteps        = num(t, "light_steps", r.lightSteps);
    r.lightStepSoftness = num(t, "light_step_softness", r.lightStepSoftness);
    r.fogDesatBoost     = num(t, "fog_desat_boost", r.fogDesatBoost);
    r.lightShaftColour  = vec4Of(t, "light_shaft_colour", r.lightShaftColour);
    r.gradeDesaturate   = num(t, "grade_desaturate", r.gradeDesaturate);
    r.gradeContrast     = num(t, "grade_contrast", r.gradeContrast);
    r.gradeShadowTint   = vec3Of(t, "grade_shadow_tint", r.gradeShadowTint);
    r.gradeMidTint      = vec3Of(t, "grade_mid_tint", r.gradeMidTint);
    r.gradeSaturation   = num(t, "grade_saturation", r.gradeSaturation);
    r.gradeTintStrength = num(t, "grade_tint_strength", r.gradeTintStrength);
    r.gradeBlackLift    = num(t, "grade_black_lift", r.gradeBlackLift);
    r.colDepth          = num(t, "col_depth", r.colDepth);
    r.ditherBanding     = num(t, "dither_banding", r.ditherBanding);
    r.ditherDarkFade    = num(t, "dither_dark_fade", r.ditherDarkFade);
    r.shadowColour      = vec3Of(t, "shadow_colour", r.shadowColour);
    r.shadowStrength    = num(t, "shadow_strength", r.shadowStrength);
    if (t.contains("highlight_colour")) {
        r.highlightColour = vec3Of(t, "highlight_colour", r.highlightColour);
        r.highlightColourOverride = true;
    }
    r.highlightStrength = num(t, "highlight_strength", r.highlightStrength);
    r.outlineColour     = vec3Of(t, "outline_colour", r.outlineColour);
    r.outlineOpacity    = num(t, "outline_opacity", r.outlineOpacity);
    r.outlineDepthSens  = num(t, "outline_depth_sens", r.outlineDepthSens);
    r.outlineNormalSens = num(t, "outline_normal_sens", r.outlineNormalSens);
    r.vignetteStrength  = num(t, "vignette_strength", r.vignetteStrength);
    r.vignetteColour    = vec3Of(t, "vignette_colour", r.vignetteColour);
    r.bloomThreshold    = num(t, "bloom_threshold", r.bloomThreshold);
    r.bloomIntensity    = num(t, "bloom_intensity", r.bloomIntensity);

    out = r;
    return true;
}

void applyRenderPalette(eng::Renderer& r, const RenderPalette& p,
                        eng::NodeHandle sunNode, eng::LightHandle sunLight) {
    const auto lin = [](float srgb) { return std::pow(srgb, 2.2f); };
    const auto lin3 = [&](glm::vec3 c) {
        return glm::vec3(lin(c.x), lin(c.y), lin(c.z));
    };

    r.setAmbient(lin3(p.ambientSrgb) * p.ambientScale);

    // A palette describes a sun, but not every scene has one: a level whose
    // authored map carries only point lights reaches here with null handles,
    // and both of these used to be called anyway -- reporting "invalid node
    // handle 0 in setOrientation" and "invalid light handle 0" on every load,
    // and then dropping the palette's sun settings on the floor regardless.
    //
    // Guarded rather than fixed by inventing a sun: whether a scene is lit by
    // one is a content decision, and the renderer conjuring a light nobody
    // authored would change how every sunless level looks.
    if (sunNode.valid())
        r.setOrientation(sunNode,
                         glm::angleAxis(glm::radians(p.sunYawDeg), glm::vec3(0, 1, 0)) *
                             glm::angleAxis(glm::radians(p.sunPitchDeg),
                                            glm::vec3(1, 0, 0)));
    if (sunLight.valid())
        r.setLightColour(sunLight, lin3(p.sunColourSrgb) * p.sunScale);
    r.setFog(lin3(p.fogSrgb), p.fogDensity);
    r.setBackground(p.backgroundSrgb); // raw sRGB, matches old applyPalette()
    r.setLightSteps(p.lightSteps);
    r.setLightStepSoftness(p.lightStepSoftness);
    r.setFogDesatBoost(p.fogDesatBoost);

    r.setMaterialParam("Game/Demo/LightShaft", "modulateColor", p.lightShaftColour);

    r.setGradeEnabled(true);
    r.setGradeParams(p.gradeDesaturate, p.gradeContrast, p.gradeShadowTint,
                     p.gradeMidTint);

    r.setMaterialParam("Engine/Psx/DitherPost", "gradeSaturation", p.gradeSaturation);
    r.setMaterialParam("Engine/Psx/DitherPost", "gradeTintStrength", p.gradeTintStrength);
    r.setMaterialParam("Engine/Psx/DitherPost", "gradeBlackLift", p.gradeBlackLift);
    r.setMaterialParam("Engine/Psx/DitherPost", "vignetteStrength", p.vignetteStrength);
    r.setMaterialParam("Engine/Psx/DitherPost", "vignetteColor", p.vignetteColour);
    r.setMaterialParam("Engine/Psx/DitherPost", "ditherEnabled", 1.0f);
    r.setMaterialParam("Engine/Psx/DitherPost", "colDepth", p.colDepth);
    r.setMaterialParam("Engine/Psx/DitherPost", "ditherBanding", p.ditherBanding);
    r.setMaterialParam("Engine/Psx/DitherPost", "ditherDarkFade", p.ditherDarkFade);

    r.setMaterialParam("Engine/Psx/PixelStylize", "shadowColor", p.shadowColour);
    r.setMaterialParam("Engine/Psx/PixelStylize", "shadowStrength", p.shadowStrength);
    r.setMaterialParam("Engine/Psx/PixelStylize", "highlightColor", p.highlightColour);
    r.setMaterialParam("Engine/Psx/PixelStylize", "highlightColorOverride",
                       p.highlightColourOverride ? 1.0f : 0.0f);
    r.setMaterialParam("Engine/Psx/PixelStylize", "highlightStrength", p.highlightStrength);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineColor", p.outlineColour);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineOpacity", p.outlineOpacity);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineDepthSens", p.outlineDepthSens);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineNormalSens", p.outlineNormalSens);

    r.setBloomParams(p.bloomThreshold, p.bloomIntensity);
}
