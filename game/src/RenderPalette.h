#pragma once

#include <eng/Handles.h>
#include <glm/glm.hpp>
#include <string>

namespace eng { class Renderer; }

// Plain-data description of one scene's look. Colours here are authored in
// sRGB (0..1) exactly as they appear in palettes.toml; applyPalette()
// linearises the ones the renderer expects in linear space (pow 2.2),
// matching the old hand-written applyPalette() conventions in main.cpp.
struct RenderPalette {
    glm::vec3 ambientSrgb{0.46f, 0.47f, 0.53f};
    float     ambientScale = 0.25f;
    float     sunYawDeg = 30.0f;
    float     sunPitchDeg = -75.0f;
    glm::vec3 sunColourSrgb{0.62f, 0.54f, 0.76f};
    float     sunScale = 0.42f;
    glm::vec3 fogSrgb{0.12f, 0.115f, 0.15f};
    float     fogDensity = 0.050f;
    glm::vec3 backgroundSrgb{0.038f, 0.035f, 0.050f};
    float lightSteps = 4.0f;
    float lightStepSoftness = 0.30f;
    float fogDesatBoost = 0.08f;
    glm::vec4 lightShaftColour{0.68f, 0.76f, 1.0f, 0.19f};
    float     gradeDesaturate = 0.015f;
    float     gradeContrast = 0.98f;
    glm::vec3 gradeShadowTint{0.12f, 0.12f, 0.18f};
    glm::vec3 gradeMidTint{0.72f, 0.65f, 0.60f};
    float     gradeSaturation = 1.0f;
    float     gradeTintStrength = 0.035f;
    float     gradeBlackLift = 0.060f;
    float colDepth = 31.0f;
    float ditherBanding = 0.018f;
    float ditherDarkFade = 0.20f;
    glm::vec3 shadowColour{0.035f, 0.025f, 0.09f};
    float     shadowStrength = 0.16f;
    glm::vec3 highlightColour{1.0f, 0.72f, 0.42f};
    float     highlightStrength = 0.10f;
    glm::vec3 outlineColour{0.025f, 0.018f, 0.065f};
    float     outlineOpacity = 0.26f;
    float     outlineDepthSens = 8.0f;
    float     outlineNormalSens = 0.20f;
    float     vignetteStrength = 0.08f;
    glm::vec3 vignetteColour{0.24f, 0.20f, 0.38f};
    float     bloomThreshold = 0.72f;
    float     bloomIntensity = 0.72f;
};

bool loadRenderPalette(const std::string& tomlPath, const std::string& name,
                       RenderPalette& out);
void applyRenderPalette(eng::Renderer& r, const RenderPalette& p,
                        eng::NodeHandle sunNode, eng::LightHandle sunLight);
