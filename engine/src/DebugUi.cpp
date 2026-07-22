#include "DebugUiImpl.h"

#include <eng/Log.h>
#include <eng/Renderer.h>

#include "RenderCore.h"
#include "RenderPresets.h"

#include <OgreCamera.h>
#include <OgreImGuiOverlay.h>
#include <OgreRenderWindow.h>

#include <imgui.h>

#include <glm/glm.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace eng {

namespace {

// sRGB <-> linear for colour widgets (engine convention: pow 2.2).
glm::vec3 toSrgb(glm::vec3 lin)
{
    return glm::pow(glm::max(lin, glm::vec3(0.0f)), glm::vec3(1.0f / 2.2f));
}
glm::vec3 toLinear(glm::vec3 srgb)
{
    return glm::pow(glm::max(srgb, glm::vec3(0.0f)), glm::vec3(2.2f));
}

} // namespace

DebugUi::DebugUi() : mImpl(new Impl) {}
DebugUi::~DebugUi() = default;

void DebugUi::Impl::init(RenderCore* c, Renderer* r)
{
    core = c;
    renderer = r;
    ctx.init(c);
    const char* requested = std::getenv("PSX_RENDER_PRESET");
    if (requested) {
        int id = renderPresetFromName(requested);
        if (id > 0) { renderPreset = id; applyRenderPreset = true; }
        else log::warn("Unknown PSX_RENDER_PRESET '%s'", requested);
    }
}

void DebugUi::addPanel(const std::string& name, std::function<void()> draw)
{
    mImpl->panels.emplace_back(name, std::move(draw));
}

void DebugUi::addWindow(std::function<void()> draw)
{
    mImpl->windows.emplace_back(std::move(draw));
}

bool DebugUi::visible() const { return mImpl->visible; }
void DebugUi::setVisible(bool v) { mImpl->visible = v; }
void DebugUi::setMainWindowVisible(bool visible)
{
    mImpl->mainWindowVisible = visible;
}

void DebugUi::setHudPrompt(const std::string& text) { mImpl->ctx.setHudPrompt(text); }

bool DebugUi::Impl::onEvent(const SDL_Event& e)
{
    if (!visible)
        return false;
    return ctx.onEvent(e);
}

void DebugUi::Impl::buildFrame(float dt)
{
    ctx.beginFrame(dt);
    ctx.drawHudPrompt();

    if (!visible)
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (mainWindowVisible) {
        const ImVec2 panelSize(std::min(380.0f, io.DisplaySize.x - 20.0f),
                               std::min(540.0f, io.DisplaySize.y - 20.0f));
        ImGui::SetNextWindowSize(panelSize, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(300, 260),
                                            ImVec2(io.DisplaySize.x - 20.0f,
                                                   io.DisplaySize.y - 20.0f));
        ImGui::Begin("Engine Diagnostics");
        if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen))
            drawStats();
        if (ImGui::CollapsingHeader("PSX Shaders"))
            drawShaders();
        if (ImGui::CollapsingHeader("Pixel Art", ImGuiTreeNodeFlags_DefaultOpen))
            drawPixelArt();
        if (ImGui::CollapsingHeader("Camera"))
            drawCamera();
        for (auto& [name, fn] : panels)
            if (ImGui::CollapsingHeader(name.c_str()))
                fn();
        if (ImGui::Button("Copy all as TOML"))
            copyToml();
        ImGui::End();
    }
    for (auto& draw : windows)
        draw();
}

void DebugUi::Impl::drawStats()
{
    // FPS from the engine-side frame-time ring: window lastFPS needs a
    // second of frames before it primes (reads 0.0 in short captures).
    const float avgMs = ctx.avgFrameMs();
    ImGui::Text("FPS %.1f (avg %.2f ms)", avgMs > 0.0f ? 1000.0f / avgMs : 0.0f,
                avgMs);
    ImGui::PlotLines("##frametimes", ctx.frames().data(),
                     int(ctx.frames().size()), ctx.frameIndex(), "frame ms",
                     0.0f, 33.0f, ImVec2(-1, 40));
    // Whole-frame counts incl. the compositor MRT scene pass (the window's
    // own statistics only cover the final quads).
    size_t batches = 0, tris = 0;
    core->frameStats(batches, tris);
    ImGui::Text("batches %zu  triangles %zu", batches, tris);
    const Ogre::Vector3 p = core->camera()->getDerivedPosition();
    ImGui::Text("window %ux%u  cam %.2f %.2f %.2f", core->window()->getWidth(),
                core->window()->getHeight(), p.x, p.y, p.z);
}

void DebugUi::Impl::drawShaders()
{
    const EnvState& env = renderer->envState();
    bool wireframe = env.wireframe;
    if (ImGui::Checkbox("wireframe view", &wireframe)) {
        renderer->setWireframeDebug(wireframe);
        // Enabling forces pixelSize 1; reapply the chosen line thickness.
        if (wireframe && wireThickness > 1)
            renderer->setPixelSize(wireThickness);
    }
    ImGui::SetItemTooltip("Debug view: every model as light-blue mesh lines\n"
                          "(PSX/DebugWireframe); original materials restore\n"
                          "on untick.");
    if (wireframe) {
        if (ImGui::ColorEdit3("wire colour", &wireColor.x))
            renderer->setMaterialParam("PSX/DebugWireframe", "wireColor",
                                       glm::vec4(wireColor, 1.0f));
        if (ImGui::SliderFloat("wire depth fade", &wireDepthFade, 0.0f, 0.5f,
                               "%.3f"))
            renderer->setMaterialParam("PSX/DebugWireframe", "wireDepthFade",
                                       wireDepthFade);
        ImGui::SetItemTooltip("exp(-depth*fade): dims distant lines so dense\n"
                              "far geometry stops reading as solid noise;\n"
                              "0 = flat colour everywhere.");
        if (ImGui::SliderInt("wire thickness (px)", &wireThickness, 1, 8))
            renderer->setPixelSize(wireThickness);
        ImGui::SetItemTooltip("GL core profile forbids wide lines, so lines\n"
                              "thicken via the pixelation RT: rendered at 1/n\n"
                              "resolution, nearest-upscaled to n-px chunks.\n"
                              "Restores your pixel size on untick.");
    }
    if (ImGui::SliderFloat("vertex snap", &precisionMultiplier, 0.10f, 1.0f,
                           "%.2f"))
        renderer->setGlobalMaterialParam("precisionMultiplier",
                                         precisionMultiplier);
    if (ImGui::Checkbox("banded lighting", &bandedLightingEnabled))
        renderer->setLightSteps(bandedLightingEnabled ? bandedLightSteps : 0.0f);
    if (ImGui::SliderFloat("light steps", &bandedLightSteps, 1.0f, 12.0f,
                           "%.0f") && bandedLightingEnabled)
        renderer->setLightSteps(bandedLightSteps);
    float lightStepSoftness = env.lightStepSoftness;
    if (ImGui::SliderFloat("light step softness", &lightStepSoftness, 0.0f,
                           0.5f, "%.2f"))
        renderer->setLightStepSoftness(lightStepSoftness);
    float fogDesat = env.fogDesatBoost;
    if (ImGui::SliderFloat("fog desat boost", &fogDesat, 0.0f, 1.0f))
        renderer->setFogDesatBoost(fogDesat);

    bool dither = env.dither;
    if (ImGui::Checkbox("dither pass", &dither))
        renderer->setDitherEnabled(dither);
    if (ImGui::SliderFloat("colour depth", &colDepth, 1.0f, 255.0f, "%.0f"))
        renderer->setMaterialParam("PSX/DitherPost", "colDepth", colDepth);
    if (ImGui::SliderFloat("dither strength", &ditherBanding, 0.0f, 1.0f,
                           "%.2f"))
        renderer->setMaterialParam("PSX/DitherPost", "ditherBanding",
                                   ditherBanding);
    if (ImGui::SliderFloat("dither dark fade", &ditherDarkFade, 0.0f, 0.3f,
                           "%.3f"))
        renderer->setMaterialParam("PSX/DitherPost", "ditherDarkFade",
                                   ditherDarkFade);

    // Colour widgets edit sRGB; engine stores ambient/fog linear.
    glm::vec3 c = toSrgb(env.ambient);
    if (ImGui::ColorEdit3("ambient", &c.x))
        renderer->setAmbient(toLinear(c));
    c = toSrgb(env.fogColour);
    float density = env.fogDensity;
    bool fogChanged = ImGui::ColorEdit3("fog colour", &c.x);
    fogChanged |= ImGui::SliderFloat("fog density", &density, 0.0f, 0.5f, "%.3f");
    if (fogChanged)
        renderer->setFog(toLinear(c), density);
    glm::vec3 bg = env.background; // raw sRGB, no conversion
    if (ImGui::ColorEdit3("background", &bg.x))
        renderer->setBackground(bg);
}

void DebugUi::Impl::drawPixelArt()
{
    const EnvState& env = renderer->envState();
    int pixelSize = env.pixelSize;
    if (ImGui::SliderInt("pixel size", &pixelSize, 1, 16))
        renderer->setPixelSize(pixelSize);
    bool perPixelLighting = env.perPixelLighting;
    if (ImGui::Checkbox("per-pixel lighting", &perPixelLighting))
        renderer->setPerPixelLightingEnabled(perPixelLighting);
    float omniAtten = env.omniAttenuation;
    if (ImGui::SliderFloat("omni attenuation", &omniAtten, 0.05f, 2.0f, "%.3f"))
        renderer->setOmniAttenuation(omniAtten);

    ImGui::SeparatorText("ink and highlights");
    if (ImGui::Checkbox("stylize pass", &stylizeEnabled))
        renderer->setMaterialParam("PSX/PixelStylize", "stylizeEnabled",
                                   stylizeEnabled ? 1.0f : 0.0f);
    ImGui::SameLine();
    if (ImGui::Button("Reset stylize")) {
        inkEnabled = highlightsEnabled = outlinesEnabled = true;
        inkStrength = 0.16f;
        inkThreshold = 0.25f;
        inkColor = {0.035f, 0.025f, 0.09f};
        highlightStrength = 0.10f;
        highlightThreshold = 0.50f;
        highlightDarkFade = 0.15f;
        highlightColor = {1.0f, 0.72f, 0.42f};
        outlineOpacity = 0.26f;
        outlineThickness = 1.0f;
        outlineDepthSensitivity = 8.0f;
        outlineNormalSensitivity = 0.20f;
        outlineSharpness = 0.85f;
        outlineDistanceFade = 0.08f;
        outlineDarkFade = 0.12f;
        outlineColor = {0.025f, 0.018f, 0.065f};
        renderer->setMaterialParam("PSX/PixelStylize", "shadowsEnabled", 1.0f);
        renderer->setMaterialParam("PSX/PixelStylize", "highlightsEnabled", 1.0f);
        renderer->setMaterialParam("PSX/PixelStylize", "outlineEnabled", 1.0f);
        renderer->setMaterialParam("PSX/PixelStylize", "shadowStrength", inkStrength);
        renderer->setMaterialParam("PSX/PixelStylize", "shadowThreshold", inkThreshold);
        renderer->setMaterialParam("PSX/PixelStylize", "shadowColor", inkColor);
        renderer->setMaterialParam("PSX/PixelStylize", "highlightStrength", highlightStrength);
        renderer->setMaterialParam("PSX/PixelStylize", "highlightThreshold", highlightThreshold);
        renderer->setMaterialParam("PSX/PixelStylize", "highlightDarkFade", highlightDarkFade);
        renderer->setMaterialParam("PSX/PixelStylize", "highlightColor", highlightColor);
        renderer->setMaterialParam("PSX/PixelStylize", "outlineOpacity", outlineOpacity);
        renderer->setMaterialParam("PSX/PixelStylize", "outlineThickness", outlineThickness);
        renderer->setMaterialParam("PSX/PixelStylize", "outlineDepthSens", outlineDepthSensitivity);
        renderer->setMaterialParam("PSX/PixelStylize", "outlineNormalSens", outlineNormalSensitivity);
        renderer->setMaterialParam("PSX/PixelStylize", "outlineSharpness", outlineSharpness);
        renderer->setMaterialParam("PSX/PixelStylize", "outlineDistFade", outlineDistanceFade);
        renderer->setMaterialParam("PSX/PixelStylize", "outlineDarkFade", outlineDarkFade);
        renderer->setMaterialParam("PSX/PixelStylize", "outlineColor", outlineColor);
    }
    if (ImGui::Checkbox("ink shadows", &inkEnabled))
        renderer->setMaterialParam("PSX/PixelStylize", "shadowsEnabled",
                                   inkEnabled ? 1.0f : 0.0f);
    ImGui::SameLine();
    if (ImGui::Checkbox("highlights", &highlightsEnabled))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightsEnabled",
                                   highlightsEnabled ? 1.0f : 0.0f);
    ImGui::SameLine();
    if (ImGui::Checkbox("outlines", &outlinesEnabled))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineEnabled",
                                   outlinesEnabled ? 1.0f : 0.0f);
    if (ImGui::SliderFloat("ink strength", &inkStrength, 0.0f, 1.0f, "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "shadowStrength",
                                   inkStrength);
    if (ImGui::SliderFloat("ink edge threshold", &inkThreshold, 0.0f, 1.0f,
                           "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "shadowThreshold",
                                   inkThreshold);
    ImGui::SetItemTooltip("Lower values place ink shadows on gentler depth edges.");
    if (ImGui::ColorEdit3("ink colour", &inkColor.x))
        renderer->setMaterialParam("PSX/PixelStylize", "shadowColor", inkColor);

    ImGui::SeparatorText("highlights");
    if (ImGui::SliderFloat("highlight strength", &highlightStrength, 0.0f,
                           0.5f, "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightStrength",
                                   highlightStrength);
    if (ImGui::SliderFloat("highlight threshold", &highlightThreshold, 0.0f,
                           1.5f, "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightThreshold",
                                   highlightThreshold);
    ImGui::SetItemTooltip("Higher values restrict highlights to sharper normal changes.");
    if (ImGui::SliderFloat("highlight dark fade", &highlightDarkFade, 0.02f,
                           0.60f, "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightDarkFade",
                                   highlightDarkFade);
    if (ImGui::ColorEdit3("highlight colour", &highlightColor.x))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightColor",
                                   highlightColor);

    ImGui::SeparatorText("outlines");
    if (ImGui::SliderFloat("outline opacity", &outlineOpacity, 0.0f, 1.0f,
                           "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineOpacity",
                                   outlineOpacity);
    if (ImGui::SliderFloat("outline thickness", &outlineThickness, 0.5f,
                           4.0f, "%.1f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineThickness",
                                   outlineThickness);
    if (ImGui::SliderFloat("depth sensitivity", &outlineDepthSensitivity, 0.0f,
                           24.0f, "%.1f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineDepthSens",
                                   outlineDepthSensitivity);
    if (ImGui::SliderFloat("normal sensitivity", &outlineNormalSensitivity,
                           0.0f, 1.5f, "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineNormalSens",
                                   outlineNormalSensitivity);
    if (ImGui::SliderFloat("edge sharpness", &outlineSharpness, 0.0f, 1.0f,
                           "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineSharpness",
                                   outlineSharpness);
    if (ImGui::SliderFloat("distance fade", &outlineDistanceFade, 0.0f, 0.30f,
                           "%.3f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineDistFade",
                                   outlineDistanceFade);
    ImGui::SetItemTooltip("Higher values remove distant line noise sooner.");
    if (ImGui::SliderFloat("outline dark fade", &outlineDarkFade, 0.02f, 0.50f,
                           "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineDarkFade",
                                   outlineDarkFade);
    if (ImGui::ColorEdit3("outline colour", &outlineColor.x))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineColor",
                                   outlineColor);

    ImGui::SeparatorText("post effects");
    if (ImGui::Checkbox("hardware resolve", &hardwareResolveEnabled))
        renderer->setMaterialParam("PSX/HardwareResolve", "resolveMode",
                                   hardwareResolveEnabled
                                       ? hardwareResolveMode : 0.0f);
    if (ImGui::SliderFloat("resolve strength", &hardwareResolveStrength, 0.0f,
                           1.0f, "%.2f"))
        renderer->setMaterialParam("PSX/HardwareResolve", "resolveStrength",
                                   hardwareResolveStrength);
    ImGui::SetItemTooltip("Era-specific reconstruction selected by the active profile.");
    bool bloom = env.bloom;
    if (ImGui::Checkbox("bloom", &bloom))
        renderer->setBloomEnabled(bloom);
    float bloomThreshold = env.bloomThreshold;
    float bloomIntensity = env.bloomIntensity;
    bool bloomChanged =
        ImGui::SliderFloat("bloom threshold", &bloomThreshold, 0.0f, 1.0f);
    bloomChanged |=
        ImGui::SliderFloat("bloom intensity", &bloomIntensity, 0.0f, 3.0f);
    if (bloomChanged)
        renderer->setBloomParams(bloomThreshold, bloomIntensity);
    if (ImGui::Checkbox("vignette", &vignetteEnabled))
        renderer->setMaterialParam("PSX/DitherPost", "vignetteStrength",
                                   vignetteEnabled ? vignetteStrength : 0.0f);
    if (ImGui::SliderFloat("vignette strength", &vignetteStrength, 0.0f, 1.0f,
                           "%.2f") && vignetteEnabled)
        renderer->setMaterialParam("PSX/DitherPost", "vignetteStrength",
                                   vignetteStrength);
    if (ImGui::ColorEdit3("vignette colour", &vignetteColor.x))
        renderer->setMaterialParam("PSX/DitherPost", "vignetteColor",
                                   vignetteColor);

    ImGui::SeparatorText("rendering profile");
    static const char* presets[] = {"Custom", "PS1 authentic", "PS2",
                                    "GameCube", "Nintendo 64",
                                    "3D pixelated", "Modern PS1"};
    const bool presetChanged =
        ImGui::Combo("preset", &renderPreset, presets, IM_ARRAYSIZE(presets));
    if ((presetChanged || applyRenderPreset) && renderPreset > 0) {
        applyRenderPreset = false;
        RenderPresetValues v = renderPresetValues(renderPreset);
        eng::applyRenderPreset(*renderer, v);
        // Sync UI-side cache so sliders reflect the applied preset.
        precisionMultiplier = v.precisionMultiplier;
        bandedLightingEnabled = v.bandedLightingEnabled;
        bandedLightSteps = v.bandedLightSteps;
        stylizeEnabled = true;
        inkEnabled = v.inkEnabled; highlightsEnabled = v.highlightsEnabled;
        outlinesEnabled = v.outlinesEnabled;
        inkStrength = v.inkStrength; inkThreshold = v.inkThreshold;
        inkColor = v.inkColor;
        highlightStrength = v.highlightStrength;
        highlightThreshold = v.highlightThreshold;
        highlightDarkFade = v.highlightDarkFade;
        highlightColor = v.highlightColor;
        outlineOpacity = v.outlineOpacity; outlineThickness = v.outlineThickness;
        outlineDepthSensitivity = v.outlineDepthSens;
        outlineNormalSensitivity = v.outlineNormalSens;
        outlineSharpness = v.outlineSharpness;
        outlineDistanceFade = v.outlineDistFade;
        outlineDarkFade = v.outlineDarkFade; outlineColor = v.outlineColor;
        gradeSaturation = v.gradeSaturation;
        gradeTintStrength = v.gradeTintStrength;
        gradeBlackLift = v.gradeBlackLift;
        vignetteStrength = v.vignetteStrength; vignetteColor = v.vignetteColor;
        colDepth = v.colDepth; ditherBanding = v.ditherBanding;
        ditherDarkFade = v.ditherDarkFade;
        hardwareResolveEnabled = v.hardwareResolveEnabled;
        hardwareResolveMode = v.hardwareResolveMode;
        hardwareResolveStrength = v.hardwareResolveStrength;
    }
    static const char* descriptions[] = {
        "Manual shader tuning.",
        "Low resolution, coarse vertex snap, vertex lighting, 5-bit colour and dither.",
        "Full-resolution geometry, smooth lighting, subtle bloom and high colour precision.",
        "Clean saturated output, smooth lighting and gentle bloom without retro noise.",
        "Low-resolution vertex lighting with restrained colour precision and no post ink.",
        "Chunky pixels, banded per-pixel light, crisp outlines and restrained bloom.",
        "PS1 foundations with softer snap, illustrated edges and selective modern bloom."
    };
    ImGui::TextWrapped("%s", descriptions[renderPreset]);
    bool grade = env.grade;
    if (ImGui::Checkbox("colour grade", &grade))
        renderer->setGradeEnabled(grade);
    float gDesat = env.gradeDesaturate;
    float gContrast = env.gradeContrast;
    glm::vec3 gShadow = env.gradeShadowTint;
    glm::vec3 gMid = env.gradeMidTint;
    bool gradeChanged = ImGui::SliderFloat("desaturate", &gDesat, 0.0f, 1.0f);
    gradeChanged |= ImGui::SliderFloat("contrast", &gContrast, 0.5f, 1.5f);
    gradeChanged |= ImGui::ColorEdit3("shadow tint", &gShadow.x);
    gradeChanged |= ImGui::ColorEdit3("mid tint", &gMid.x);
    if (gradeChanged) {
        renderPreset = 0;
        renderer->setGradeParams(gDesat, gContrast, gShadow, gMid);
    }
    bool paletteChanged =
        ImGui::SliderFloat("saturation", &gradeSaturation, 0.0f, 1.5f, "%.2f");
    paletteChanged |= ImGui::SliderFloat("split-tone strength",
                                          &gradeTintStrength, 0.0f, 0.5f,
                                          "%.3f");
    paletteChanged |= ImGui::SliderFloat("black lift", &gradeBlackLift, 0.0f,
                                          0.20f, "%.3f");
    if (paletteChanged) {
        renderPreset = 0;
        renderer->setMaterialParam("PSX/DitherPost", "gradeSaturation",
                                   gradeSaturation);
        renderer->setMaterialParam("PSX/DitherPost", "gradeTintStrength",
                                   gradeTintStrength);
        renderer->setMaterialParam("PSX/DitherPost", "gradeBlackLift",
                                   gradeBlackLift);
    }
}

void DebugUi::Impl::drawCamera()
{
    const EnvState& env = renderer->envState();
    float fov = env.fovDeg;
    if (ImGui::SliderFloat("FOV (deg)", &fov, 30.0f, 120.0f, "%.0f"))
        renderer->setCameraFov(fov);
    float nearClip = env.nearClip;
    float farClip = env.farClip;
    bool clipChanged = ImGui::SliderFloat("near plane", &nearClip, 0.01f, 1.0f,
                                           "%.3f", ImGuiSliderFlags_Logarithmic);
    clipChanged |= ImGui::SliderFloat("far plane", &farClip,
                                      nearClip + 1.0f, 500.0f, "%.1f",
                                      ImGuiSliderFlags_Logarithmic);
    if (clipChanged)
        renderer->setCameraClip(nearClip, farClip);
}

void DebugUi::Impl::copyToml()
{
    const EnvState& env = renderer->envState();
    char buf[4096];
    std::snprintf(buf, sizeof(buf),
                  "[debug_tuning]\n"
                  "precision_multiplier = %.4f\n"
                  "dither = %s\n"
                  "col_depth = %.1f\n"
                  "dither_banding = %.2f\n"
                  "dither_dark_fade = %.3f\n"
                  "pixel_size = %d\n"
                  "per_pixel_lighting = %s\n"
                  "omni_attenuation = %.4f\n"
                  "bloom = %s\n"
                  "bloom_threshold = %.2f\n"
                  "bloom_intensity = %.2f\n"
                  "ambient_linear = [%.4f, %.4f, %.4f]\n"
                  "fog_colour_linear = [%.4f, %.4f, %.4f]\n"
                  "fog_density = %.4f\n"
                  "fog_desat_boost = %.4f\n"
                  "background_srgb = [%.4f, %.4f, %.4f]\n"
                  "light_steps = %.0f\n"
                  "fov_deg = %.1f\n"
                  "near_clip = %.3f\n"
                  "far_clip = %.1f\n"
                  "colour_grade = %s\n"
                  "grade_desaturate = %.4f\n"
                  "grade_contrast = %.4f\n"
                  "grade_shadow_tint = [%.4f, %.4f, %.4f]\n"
                  "grade_mid_tint = [%.4f, %.4f, %.4f]\n",
                  precisionMultiplier, env.dither ? "true" : "false", colDepth,
                  ditherBanding, ditherDarkFade,
                  env.pixelSize, env.perPixelLighting ? "true" : "false",
                  env.omniAttenuation,
                  env.bloom ? "true" : "false", env.bloomThreshold,
                  env.bloomIntensity,
                  env.ambient.x, env.ambient.y,
                  env.ambient.z, env.fogColour.x, env.fogColour.y,
                  env.fogColour.z, env.fogDensity, env.fogDesatBoost,
                  env.background.x, env.background.y, env.background.z,
                  env.lightSteps, env.fovDeg, env.nearClip, env.farClip,
                  env.grade ? "true" : "false",
                  env.gradeDesaturate, env.gradeContrast,
                  env.gradeShadowTint.x, env.gradeShadowTint.y,
                  env.gradeShadowTint.z,
                  env.gradeMidTint.x, env.gradeMidTint.y,
                  env.gradeMidTint.z);
    SDL_SetClipboardText(buf);
    log::info("DebugUi: copied to clipboard:\n%s", buf);
}

} // namespace eng
