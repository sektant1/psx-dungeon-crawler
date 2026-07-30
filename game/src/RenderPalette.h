#pragma once

#include <eng/Handles.h>
#include <glm/glm.hpp>
#include <string>

namespace eng { class Renderer; }

// Plain-data description of one scene's look. Colours here are authored in
// sRGB (0..1) exactly as they appear in palettes.toml; applyPalette()
// linearises the ones the renderer expects in linear space (pow 2.2),
// matching the old hand-written applyPalette() conventions in main.cpp.
// These defaults ARE the dungeon look, and palettes.toml relies on that: a
// table like [palette.showroom] lists only its deltas and inherits the rest from
// here. So these must stay in step with [palette.dungeon] in palettes.toml and
// with the engine's "dungeon" render profile (RenderPresets.cpp case 7).
// Rationale for the values lives in the palettes.toml header.
struct RenderPalette {
    glm::vec3 ambientSrgb{0.40f, 0.44f, 0.56f}; // cold, faintly blue
    float     ambientScale = 0.085f;            // torches do the work
    float     sunYawDeg = 30.0f;
    float     sunPitchDeg = -75.0f;
    // A dungeon has no sun; this is a dim cold fill from above so ceilings and
    // floors do not read as the same surface.
    glm::vec3 sunColourSrgb{0.50f, 0.56f, 0.78f};
    float     sunScale = 0.16f;
    glm::vec3 fogSrgb{0.055f, 0.058f, 0.080f};
    float     fogDensity = 0.070f;
    glm::vec3 backgroundSrgb{0.014f, 0.014f, 0.022f};
    float lightSteps = 0.0f; // smooth firelight; bands draw contours on floors
    float lightStepSoftness = 0.30f;
    float fogDesatBoost = 0.65f; // distance sinks to black, not to grey
    glm::vec4 lightShaftColour{0.72f, 0.66f, 0.52f, 0.16f}; // warm dust
    float     gradeDesaturate = 0.12f;
    float     gradeContrast = 1.12f;
    glm::vec3 gradeShadowTint{0.060f, 0.070f, 0.130f}; // cold blue-black
    glm::vec3 gradeMidTint{0.58f, 0.55f, 0.50f};       // damp stone
    float     gradeSaturation = 0.88f;
    float     gradeTintStrength = 0.09f;
    float     gradeBlackLift = 0.028f; // keeps shapes legible under the crush
    float colDepth = 63.0f;
    float ditherBanding = 0.022f;
    float ditherDarkFade = 0.28f;
    glm::vec3 shadowColour{0.020f, 0.020f, 0.035f};
    float     shadowStrength = 0.20f;
    glm::vec3 highlightColour{1.0f, 0.70f, 0.40f}; // convex edges catch torches
    bool      highlightColourOverride = false;
    float     highlightStrength = 0.10f;
    glm::vec3 outlineColour{0.015f, 0.015f, 0.028f};
    float     outlineOpacity = 0.30f; // silhouette vs. the black behind it
    float     outlineDepthSens = 9.0f;
    float     outlineNormalSens = 0.22f;
    float     vignetteStrength = 0.28f;
    glm::vec3 vignetteColour{0.090f, 0.090f, 0.150f};
    float     bloomThreshold = 0.62f;
    float     bloomIntensity = 0.55f;
};

bool loadRenderPalette(const std::string& tomlPath, const std::string& name,
                       RenderPalette& out);
void applyRenderPalette(eng::Renderer& r, const RenderPalette& p,
                        eng::NodeHandle sunNode, eng::LightHandle sunLight);
