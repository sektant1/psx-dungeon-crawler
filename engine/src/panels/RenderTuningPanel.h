#pragma once
#include <glm/glm.hpp>

namespace eng { class Renderer; class RenderCore; }
namespace eng::ui { class Context; }

namespace eng::ui {

// Renderer/PSX-shader/pixel-art/camera tuning UI. Extracted from DebugUi so both
// the game debug HUD and the editor "Settings" panel share it. Owns its own
// UI-side cache mirroring the shader defaults.
class RenderTuningPanel {
public:
    void init(Renderer* r);                 // reads PSX_RENDER_PRESET env
    void drawStats(const Context& ctx);     // FPS + frametime plot + batch/tri
    void drawShaders();
    void drawPixelArt();
    void drawCamera();
    void copyToml();
    // Convenience composite: the four CollapsingHeaders + "Copy all as TOML"
    // button, identical to the old Engine Diagnostics body. Used by the editor.
    void drawAll(const Context& ctx);
private:
    Renderer* mR = nullptr;
    RenderCore* mCore = nullptr; // from ctx during draws that need frameStats/camera

    // PSX shader tunables (UI-side cache; initial values match the
    // defaults in engine/assets/programs/psx.program + psx.material).
    float precisionMultiplier = 1.0f;
    // Wireframe debug view (Wire_FS defaults).
    glm::vec3 wireColor{0.55f, 0.8f, 1.0f};
    float wireDepthFade = 0.0f;
    // Line thickness in screen pixels, faked via the pixelation RT (GL core
    // profile forbids glLineWidth > 1): render at 1/n res, nearest upscale.
    int wireThickness = 1;
    float colDepth = 31.0f;
    float ditherBanding = 0.018f; // pattern amplitude 0..1, not just on/off
    float ditherDarkFade = 0.20f;
    bool bandedLightingEnabled = true;
    float bandedLightSteps = 4.0f;
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
    float outlineDepthSensitivity = 8.0f;
    float outlineNormalSensitivity = 0.20f;
    float outlineSharpness = 0.85f;
    float outlineDistanceFade = 0.08f;
    float outlineDarkFade = 0.12f;
    glm::vec3 outlineColor{0.025f, 0.018f, 0.065f};
    bool vignetteEnabled = true;
    float vignetteStrength = 0.08f;
    glm::vec3 vignetteColor{0.24f, 0.20f, 0.38f};
    int renderPreset = 6;
    bool applyRenderPreset = false;
    float gradeSaturation = 1.0f;
    float gradeTintStrength = 0.035f;
    float gradeBlackLift = 0.060f;
    bool hardwareResolveEnabled = false;
    float hardwareResolveMode = 0.0f;
    float hardwareResolveStrength = 0.65f;
};

} // namespace eng::ui
