#pragma once
#include <eng/RenderPresetInfo.h> // public by-id API; this header is the tuning detail

#include <glm/glm.hpp>

namespace eng {
class Renderer;

// Plain-data description of one render preset. Field names mirror the UI cache
// in DebugUiImpl.h so the debug panel can copy a preset straight into its
// sliders.
struct RenderPresetValues {
    int pixelSize = 3;
    // Non-zero: render at this absolute resolution instead of window/pixelSize.
    // The console profiles use it to sit on their real framebuffer regardless of
    // window size. Pixel-art profiles deliberately stay on the pixelSize
    // divisor, because only an integer divisor keeps the upscale on a pixel grid
    // -- an absolute size the window isn't a multiple of gives uneven pixels.
    int targetWidth = 0, targetHeight = 0;
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
    // False keeps highlights in the surface's own hue. True uses the authored
    // highlightColor as a global tint.
    bool highlightColorOverride = false;
    glm::vec3 highlightColor{1.0f, 0.72f, 0.42f};
    float outlineOpacity = 0.26f;
    float outlineThickness = 1.0f;
    float outlineDepthSens = 8.0f;
    float outlineNormalSens = 0.20f;
    float outlineSharpness = 0.85f;
    float outlineDistFade = 0.08f;
    float outlineDarkFade = 0.12f;
    glm::vec3 outlineColor{0.025f, 0.018f, 0.065f};
    // 1 = edge highlights fire on convex folds only and creases on concave
    // ones (the 3D-pixel-art convention); 0 = both react to any normal
    // discontinuity. Bias is the deadzone that keeps shallow curvature out of
    // both terms so it cannot flicker between them under camera motion.
    float edgeConvexity = 1.0f;
    float edgeConvexBias = 0.05f;

    bool bloom = true;
    float bloomThreshold = 0.72f;
    float bloomIntensity = 0.72f;
    // 1 = snap the glow lookup to the render-pixel grid (pixel-art profiles);
    // 0 = smooth bilinear upsample (the PS2/GameCube profiles want this).
    float bloomPixelSnap = 0.0f;

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

    // Distance desaturate/darken before the fog mix. Negative = "leave the
    // scene's own value alone", which is the default: this one is authored
    // per-level by the game's render palette, so only a profile that means to
    // own the whole look (the dungeon default) sets it.
    float fogDesatBoost = -1.0f;
};


// Returns the tuned values for preset id 1..7. Any other id returns defaults.
RenderPresetValues renderPresetValues(int preset);

// Pushes every value in v to the renderer (mirrors the old DebugUi apply block).
void applyRenderPreset(Renderer& renderer, const RenderPresetValues& v);

} // namespace eng
