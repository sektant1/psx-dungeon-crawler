#pragma once
#include <glm/glm.hpp>

namespace eng {
class Renderer;

// Plain-data description of one render preset. Field names mirror the UI cache
// in DebugUiImpl.h so the debug panel can copy a preset straight into its
// sliders.
struct RenderPresetValues {
    int pixelSize = 3;
    float precisionMultiplier = 1.0f;
    bool perPixel = true;
    bool bandedLightingEnabled = true;
    float bandedLightSteps = 4.0f;
    float stepSoftness = 0.30f;

    bool stylizeEnabled = true;
    bool inkEnabled = true;
    bool highlightsEnabled = true;
    bool outlinesEnabled = true;
    float inkStrength = 0.16f;
    float inkThreshold = 0.25f;
    glm::vec3 inkColor{0.035f, 0.025f, 0.09f};
    float highlightStrength = 0.10f;
    float highlightThreshold = 0.50f;
    float highlightDarkFade = 0.15f;
    glm::vec3 highlightColor{1.0f, 0.72f, 0.42f};
    float outlineOpacity = 0.26f;
    float outlineThickness = 1.0f;
    float outlineDepthSens = 8.0f;
    float outlineNormalSens = 0.20f;
    float outlineSharpness = 0.85f;
    float outlineDistFade = 0.08f;
    float outlineDarkFade = 0.12f;
    glm::vec3 outlineColor{0.025f, 0.018f, 0.065f};

    bool bloom = true;
    float bloomThreshold = 0.72f;
    float bloomIntensity = 0.72f;

    float gradeDesaturate = 0.015f;
    float gradeContrast = 0.98f;
    glm::vec3 gradeShadow{0.12f, 0.12f, 0.18f};
    glm::vec3 gradeMid{0.72f, 0.65f, 0.60f};
    float gradeSaturation = 1.0f;
    float gradeTintStrength = 0.035f;
    float gradeBlackLift = 0.060f;

    bool vignetteEnabled = true;
    float vignetteStrength = 0.08f;
    glm::vec3 vignetteColor{0.24f, 0.20f, 0.38f};

    float colDepth = 31.0f;
    float ditherBanding = 0.018f;
    float ditherDarkFade = 0.20f;

    bool hardwareResolveEnabled = true;
    float hardwareResolveMode = 6.0f; // == preset id
    float hardwareResolveStrength = 0.65f;

    float affineAmount = 0.0f; // 0 = perspective-correct, 1 = full affine warp
};

// "ps1".."modern-ps1" -> 1..6, unknown -> -1.
int renderPresetFromName(const char* name);

// Returns the tuned values for preset id 1..6. Any other id returns defaults.
RenderPresetValues renderPresetValues(int preset);

// Pushes every value in v to the renderer (mirrors the old DebugUi apply block).
void applyRenderPreset(Renderer& renderer, const RenderPresetValues& v);

} // namespace eng
