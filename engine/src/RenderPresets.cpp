#include "RenderPresets.h"
#include "eng/Renderer.h"
#include <cstring>

namespace eng {

int renderPresetFromName(const char* name)
{
    if (!name) return -1;
    if (!std::strcmp(name, "ps1")) return 1;
    if (!std::strcmp(name, "ps2")) return 2;
    if (!std::strcmp(name, "gamecube")) return 3;
    if (!std::strcmp(name, "n64")) return 4;
    if (!std::strcmp(name, "pixel-3d")) return 5;
    if (!std::strcmp(name, "modern-ps1")) return 6;
    return -1;
}

RenderPresetValues renderPresetValues(int preset)
{
    RenderPresetValues v;
    v.hardwareResolveMode = float(preset);
    switch (preset) {
    case 1: // PS1
        v.pixelSize = 3; v.perPixel = false; v.bloom = false;
        v.precisionMultiplier = 0.50f;
        v.bandedLightSteps = 4.0f; v.stepSoftness = 0.07f;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = false;
        v.gradeDesaturate = 0.06f; v.gradeContrast = 1.0f;
        v.gradeShadow = {0.13f, 0.13f, 0.16f}; v.gradeMid = {0.64f, 0.62f, 0.58f};
        v.gradeSaturation = 0.92f; v.gradeTintStrength = 0.035f;
        v.gradeBlackLift = 0.035f; v.vignetteStrength = 0.05f;
        v.vignetteColor = {0.24f, 0.23f, 0.28f};
        v.ditherBanding = 0.040f;
        v.hardwareResolveStrength = 0.55f;
        break;
    case 2: // PS2
        v.pixelSize = 1; v.perPixel = true;
        v.bandedLightingEnabled = false; v.stepSoftness = 0.35f;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = false;
        v.bloomThreshold = 0.84f; v.bloomIntensity = 0.25f;
        v.gradeDesaturate = 0.015f; v.gradeContrast = 1.0f;
        v.gradeTintStrength = 0.01f; v.gradeBlackLift = 0.025f;
        v.vignetteStrength = 0.025f; v.colDepth = 63.0f; v.ditherBanding = 0.003f;
        v.hardwareResolveStrength = 0.55f;
        break;
    case 3: // GameCube
        v.pixelSize = 1; v.perPixel = true; v.bandedLightingEnabled = false;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = false;
        v.bloomThreshold = 0.76f; v.bloomIntensity = 0.38f;
        v.gradeDesaturate = 0.0f; v.gradeContrast = 1.01f;
        v.gradeSaturation = 1.12f; v.gradeTintStrength = 0.0f;
        v.gradeBlackLift = 0.020f; v.vignetteStrength = 0.015f;
        v.colDepth = 127.0f; v.ditherBanding = 0.0f;
        v.hardwareResolveStrength = 0.70f;
        break;
    case 4: // N64
        v.pixelSize = 2; v.perPixel = false; v.bloom = false;
        v.precisionMultiplier = 0.72f; v.bandedLightingEnabled = false;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = false;
        v.gradeDesaturate = 0.035f; v.gradeContrast = 0.98f;
        v.gradeSaturation = 0.96f; v.gradeTintStrength = 0.02f;
        v.gradeBlackLift = 0.040f; v.vignetteStrength = 0.025f;
        v.colDepth = 31.0f; v.ditherBanding = 0.004f;
        v.hardwareResolveStrength = 0.78f;
        break;
    case 5: // pixel-3d
        v.pixelSize = 3; v.perPixel = true;
        v.bandedLightSteps = 5.0f; v.stepSoftness = 0.20f;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = true;
        v.inkStrength = 0.24f; v.inkThreshold = 0.20f;
        v.inkColor = {0.025f, 0.045f, 0.10f};
        v.highlightStrength = 0.18f; v.highlightThreshold = 0.34f;
        v.highlightDarkFade = 0.12f;
        v.highlightColor = {0.48f, 0.78f, 1.0f};
        v.outlineOpacity = 0.52f; v.outlineDepthSens = 10.0f;
        v.outlineNormalSens = 0.38f; v.outlineSharpness = 0.82f;
        v.outlineDistFade = 0.045f; v.outlineDarkFade = 0.09f;
        v.outlineColor = {0.018f, 0.035f, 0.09f};
        v.bloomThreshold = 0.82f; v.bloomIntensity = 0.35f;
        v.gradeDesaturate = 0.025f; v.gradeContrast = 1.01f;
        v.gradeSaturation = 1.04f; v.gradeTintStrength = 0.025f;
        v.gradeBlackLift = 0.045f; v.vignetteStrength = 0.05f;
        v.colDepth = 63.0f; v.ditherBanding = 0.008f;
        v.hardwareResolveStrength = 0.62f;
        break;
    case 6: // modern-ps1
        v.pixelSize = 3; v.perPixel = false;
        v.precisionMultiplier = 0.65f;
        v.bandedLightSteps = 4.0f; v.stepSoftness = 0.18f;
        v.inkStrength = 0.13f; v.highlightStrength = 0.055f;
        v.outlineOpacity = 0.18f; v.outlineNormalSens = 0.14f;
        v.bloomThreshold = 0.80f; v.bloomIntensity = 0.40f;
        v.ditherBanding = 0.018f;
        v.hardwareResolveStrength = 0.45f;
        break;
    default: break;
    }
    return v;
}

void applyRenderPreset(Renderer& r, const RenderPresetValues& v)
{
    r.setPixelSize(v.pixelSize);
    r.setGlobalMaterialParam("precisionMultiplier", v.precisionMultiplier);
    r.setGlobalMaterialParam("affineAmount", v.affineAmount);
    r.setPerPixelLightingEnabled(v.perPixel);
    r.setLightSteps(v.bandedLightingEnabled ? v.bandedLightSteps : 0.0f);
    r.setLightStepSoftness(v.stepSoftness);
    r.setDitherEnabled(true);
    r.setBloomEnabled(v.bloom);
    r.setBloomParams(v.bloomThreshold, v.bloomIntensity);

    r.setMaterialParam("PSX/PixelStylize", "stylizeEnabled", 1.0f);
    r.setMaterialParam("PSX/PixelStylize", "shadowsEnabled", v.inkEnabled ? 1.0f : 0.0f);
    r.setMaterialParam("PSX/PixelStylize", "highlightsEnabled", v.highlightsEnabled ? 1.0f : 0.0f);
    r.setMaterialParam("PSX/PixelStylize", "outlineEnabled", v.outlinesEnabled ? 1.0f : 0.0f);
    r.setMaterialParam("PSX/PixelStylize", "shadowStrength", v.inkStrength);
    r.setMaterialParam("PSX/PixelStylize", "shadowThreshold", v.inkThreshold);
    r.setMaterialParam("PSX/PixelStylize", "shadowColor", v.inkColor);
    r.setMaterialParam("PSX/PixelStylize", "highlightStrength", v.highlightStrength);
    r.setMaterialParam("PSX/PixelStylize", "highlightThreshold", v.highlightThreshold);
    r.setMaterialParam("PSX/PixelStylize", "highlightDarkFade", v.highlightDarkFade);
    r.setMaterialParam("PSX/PixelStylize", "highlightColor", v.highlightColor);
    r.setMaterialParam("PSX/PixelStylize", "outlineOpacity", v.outlineOpacity);
    r.setMaterialParam("PSX/PixelStylize", "outlineThickness", v.outlineThickness);
    r.setMaterialParam("PSX/PixelStylize", "outlineDepthSens", v.outlineDepthSens);
    r.setMaterialParam("PSX/PixelStylize", "outlineNormalSens", v.outlineNormalSens);
    r.setMaterialParam("PSX/PixelStylize", "outlineSharpness", v.outlineSharpness);
    r.setMaterialParam("PSX/PixelStylize", "outlineDistFade", v.outlineDistFade);
    r.setMaterialParam("PSX/PixelStylize", "outlineDarkFade", v.outlineDarkFade);
    r.setMaterialParam("PSX/PixelStylize", "outlineColor", v.outlineColor);

    r.setGradeEnabled(true);
    r.setGradeParams(v.gradeDesaturate, v.gradeContrast, v.gradeShadow, v.gradeMid);
    r.setMaterialParam("PSX/DitherPost", "gradeSaturation", v.gradeSaturation);
    r.setMaterialParam("PSX/DitherPost", "gradeTintStrength", v.gradeTintStrength);
    r.setMaterialParam("PSX/DitherPost", "gradeBlackLift", v.gradeBlackLift);
    r.setMaterialParam("PSX/DitherPost", "vignetteStrength",
                       v.vignetteEnabled ? v.vignetteStrength : 0.0f);
    r.setMaterialParam("PSX/DitherPost", "vignetteColor", v.vignetteColor);
    r.setMaterialParam("PSX/DitherPost", "colDepth", v.colDepth);
    r.setMaterialParam("PSX/DitherPost", "ditherBanding", v.ditherBanding);
    r.setMaterialParam("PSX/DitherPost", "ditherDarkFade", v.ditherDarkFade);

    r.setMaterialParam("PSX/HardwareResolve", "resolveMode", v.hardwareResolveMode);
    r.setMaterialParam("PSX/HardwareResolve", "resolveStrength", v.hardwareResolveStrength);
}

} // namespace eng
