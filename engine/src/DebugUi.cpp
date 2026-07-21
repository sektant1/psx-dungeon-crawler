#include "DebugUiImpl.h"

#include <eng/Log.h>
#include <eng/Renderer.h>

#include "RenderCore.h"

#include <OgreCamera.h>
#include <OgreImGuiOverlay.h>
#include <OgreRenderWindow.h>

#include <imgui.h>

#include <glm/glm.hpp>

#include <cstdio>
#include <string>

namespace eng {

namespace {

ImGuiKey toImGuiKey(SDL_Keycode kc)
{
    if (kc >= SDLK_a && kc <= SDLK_z)
        return ImGuiKey(ImGuiKey_A + (kc - SDLK_a));
    if (kc >= SDLK_0 && kc <= SDLK_9)
        return ImGuiKey(ImGuiKey_0 + (kc - SDLK_0));
    if (kc >= SDLK_F1 && kc <= SDLK_F12)
        return ImGuiKey(ImGuiKey_F1 + (kc - SDLK_F1));
    switch (kc) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_INSERT: return ImGuiKey_Insert;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_MINUS: return ImGuiKey_Minus;
    case SDLK_EQUALS: return ImGuiKey_Equal;
    case SDLK_COMMA: return ImGuiKey_Comma;
    case SDLK_PERIOD: return ImGuiKey_Period;
    case SDLK_SLASH: return ImGuiKey_Slash;
    case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl;
    case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_RSHIFT: return ImGuiKey_RightShift;
    case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_RALT: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}

int toImGuiMouseButton(Uint8 sdlButton)
{
    switch (sdlButton) {
    case SDL_BUTTON_LEFT: return 0;
    case SDL_BUTTON_RIGHT: return 1;
    case SDL_BUTTON_MIDDLE: return 2;
    default: return -1;
    }
}

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

void DebugUi::addPanel(const std::string& name, std::function<void()> draw)
{
    mImpl->panels.emplace_back(name, std::move(draw));
}

bool DebugUi::visible() const { return mImpl->visible; }
void DebugUi::setVisible(bool v) { mImpl->visible = v; }

void DebugUi::setHudPrompt(const std::string& text) { mImpl->hudPrompt = text; }

bool DebugUi::Impl::onEvent(const SDL_Event& e)
{
    if (!visible)
        return false;
    ImGuiIO& io = ImGui::GetIO();
    switch (e.type) {
    case SDL_MOUSEMOTION:
        io.AddMousePosEvent(float(e.motion.x), float(e.motion.y));
        return io.WantCaptureMouse;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        const int b = toImGuiMouseButton(e.button.button);
        if (b >= 0)
            io.AddMouseButtonEvent(b, e.type == SDL_MOUSEBUTTONDOWN);
        return io.WantCaptureMouse;
    }
    case SDL_MOUSEWHEEL:
        io.AddMouseWheelEvent(e.wheel.preciseX, e.wheel.preciseY);
        return io.WantCaptureMouse;
    case SDL_TEXTINPUT:
        io.AddInputCharactersUTF8(e.text.text);
        return io.WantCaptureKeyboard;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        const SDL_Keymod m = SDL_GetModState();
        io.AddKeyEvent(ImGuiMod_Ctrl, (m & KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (m & KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (m & KMOD_ALT) != 0);
        const ImGuiKey k = toImGuiKey(e.key.keysym.sym);
        if (k != ImGuiKey_None)
            io.AddKeyEvent(k, e.type == SDL_KEYDOWN);
        // KEYUP must always reach eng::Input too, or a key held while the
        // panel opens would stick down; Engine::tick handles that split.
        return e.type == SDL_KEYDOWN && io.WantCaptureKeyboard;
    }
    default:
        return false;
    }
}

void DebugUi::Impl::buildFrame(float dt)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(core->window()->getWidth()),
                            float(core->window()->getHeight()));
    Ogre::ImGuiOverlay::NewFrame();
    if (!core->imguiOverlay()->isVisible())
        core->imguiOverlay()->show(); // safe now: NewFrame has run

    frameMs[size_t(frameMsIdx)] = dt * 1000.0f;
    frameMsIdx = (frameMsIdx + 1) % int(frameMs.size());

    // HUD prompt: bottom-centre, borderless, always on top of the scene.
    if (!hudPrompt.empty()) {
        const ImVec2 ds = io.DisplaySize;
        const ImVec2 ts = ImGui::CalcTextSize(hudPrompt.c_str());
        ImGui::SetNextWindowPos(
            ImVec2((ds.x - ts.x) * 0.5f, ds.y * 0.78f));
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##hudprompt", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted(hudPrompt.c_str());
        ImGui::End();
    }

    if (!visible)
        return;

    ImGui::SetNextWindowSize(ImVec2(380, 540), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug");
    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen))
        drawStats();
    if (ImGui::CollapsingHeader("PSX Shaders"))
        drawShaders();
    if (ImGui::CollapsingHeader("Pixel Art"))
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

void DebugUi::Impl::drawStats()
{
    const auto& st = core->window()->getStatistics();
    ImGui::Text("FPS %.1f (avg %.1f)", st.lastFPS, st.avgFPS);
    ImGui::PlotLines("##frametimes", frameMs.data(), int(frameMs.size()),
                     frameMsIdx, "frame ms", 0.0f, 33.0f, ImVec2(-1, 40));
    ImGui::Text("batches %zu  triangles %zu", size_t(st.batchCount),
                size_t(st.triangleCount));
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
    if (ImGui::SliderFloat("vertex snap", &precisionMultiplier, 0.0f, 1.0f))
        renderer->setGlobalMaterialParam("precisionMultiplier",
                                         precisionMultiplier);
    float lightSteps = env.lightSteps;
    if (ImGui::SliderFloat("light steps", &lightSteps, 0.0f, 12.0f, "%.0f"))
        renderer->setLightSteps(lightSteps);
    float fogDesat = env.fogDesatBoost;
    if (ImGui::SliderFloat("fog desat boost", &fogDesat, 0.0f, 1.0f))
        renderer->setFogDesatBoost(fogDesat);

    bool dither = env.dither;
    if (ImGui::Checkbox("dither pass", &dither))
        renderer->setDitherEnabled(dither);
    if (ImGui::SliderFloat("colour depth", &colDepth, 1.0f, 64.0f, "%.0f"))
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
    bool stylize = env.stylize;
    if (ImGui::Checkbox("stylize (outline+highlight)", &stylize))
        renderer->setStylizeEnabled(stylize);
    if (ImGui::Checkbox("shadows", &stylizeShadows))
        renderer->setMaterialParam("PSX/PixelStylize", "shadowsEnabled",
                                   stylizeShadows ? 1.0f : 0.0f);
    if (ImGui::Checkbox("highlights", &stylizeHighlights))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightsEnabled",
                                   stylizeHighlights ? 1.0f : 0.0f);
    if (ImGui::SliderFloat("shadow strength", &shadowStrength, 0.0f, 1.0f))
        renderer->setMaterialParam("PSX/PixelStylize", "shadowStrength",
                                   shadowStrength);
    if (ImGui::SliderFloat("highlight strength", &highlightStrength, 0.0f, 1.0f))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightStrength",
                                   highlightStrength);
    if (ImGui::ColorEdit3("shadow colour", &shadowColor.x))
        renderer->setMaterialParam("PSX/PixelStylize", "shadowColor", shadowColor);
    if (ImGui::ColorEdit3("highlight colour", &highlightColor.x))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightColor",
                                   highlightColor);
    if (ImGui::SliderFloat("outline thickness", &outlineThickness, 1.0f, 4.0f,
                           "%.1f px"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineThickness",
                                   outlineThickness);
    ImGui::SetItemTooltip("Edge sample offset in low-res pixels; 1 = the\n"
                          "reference single-pixel outline, higher fattens\n"
                          "both shadow and highlight edges.");
    if (ImGui::SliderFloat("shadow edge threshold", &shadowThreshold, 0.05f,
                           0.6f, "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "shadowThreshold",
                                   shadowThreshold);
    ImGui::SetItemTooltip("Depth-step sensitivity: lower outlines every small\n"
                          "ledge, higher keeps only strong silhouettes.");
    if (ImGui::SliderFloat("highlight edge threshold", &highlightThreshold,
                           0.3f, 0.9f, "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightThreshold",
                                   highlightThreshold);
    ImGui::SetItemTooltip("Crease sensitivity: lower highlights shallow\n"
                          "bevels, higher only sharp convex corners.");
    if (ImGui::SliderFloat("highlight dark fade", &highlightDarkFade, 0.05f,
                           0.6f, "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightDarkFade",
                                   highlightDarkFade);
    ImGui::SetItemTooltip("Scene luma below which edge highlights fade out,\n"
                          "so dark geometry doesn't grow glowing wires.");

    ImGui::SeparatorText("ink outline");
    if (ImGui::Checkbox("ink outline", &outlineEnabled))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineEnabled",
                                   outlineEnabled ? 1.0f : 0.0f);
    ImGui::SetItemTooltip("Hard contour pass (Boltgun look): silhouette ink\n"
                          "from depth steps + interior lines from creases.\n"
                          "Drawn over the soft shadow/highlight tinting.");
    if (ImGui::ColorEdit3("outline colour", &outlineColor.x))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineColor",
                                   outlineColor);
    if (ImGui::SliderFloat("outline opacity", &outlineOpacity, 0.0f, 1.0f,
                           "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineOpacity",
                                   outlineOpacity);
    if (ImGui::SliderFloat("outline depth sens", &outlineDepthSens, 1.0f,
                           40.0f, "%.1f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineDepthSens",
                                   outlineDepthSens);
    ImGui::SetItemTooltip("Gain on relative depth steps ((far-near)/near):\n"
                          "higher inks smaller ledges; silhouettes against\n"
                          "distant walls saturate either way.");
    if (ImGui::SliderFloat("outline normal sens", &outlineNormalSens, 0.0f,
                           2.0f, "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineNormalSens",
                                   outlineNormalSens);
    ImGui::SetItemTooltip("Interior feature lines from surface creases;\n"
                          "0 = silhouette-only ink.");
    if (ImGui::SliderFloat("outline sharpness", &outlineSharpness, 0.0f, 1.0f,
                           "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineSharpness",
                                   outlineSharpness);
    ImGui::SetItemTooltip("1 = hard pixel step, lower feathers the line.");
    if (ImGui::SliderFloat("outline distance fade", &outlineDistFade, 0.0f,
                           0.3f, "%.3f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineDistFade",
                                   outlineDistFade);
    ImGui::SetItemTooltip("exp(-depth*fade): sinks far ink into the fog so\n"
                          "black wires don't float in the murk. Match the\n"
                          "scene fog density as a starting point.");
    if (ImGui::SliderFloat("outline dark fade", &outlineDarkFade, 0.03f, 0.5f,
                           "%.2f"))
        renderer->setMaterialParam("PSX/PixelStylize", "outlineDarkFade",
                                   outlineDarkFade);
    ImGui::SetItemTooltip("Scene luma below which ink fades out, so outlines\n"
                          "never draw over shadows/darkness/fog.");

    ImGui::SeparatorText("grade");
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
    if (gradeChanged)
        renderer->setGradeParams(gDesat, gContrast, gShadow, gMid);
}

void DebugUi::Impl::drawCamera()
{
    const EnvState& env = renderer->envState();
    float fov = env.fovDeg;
    if (ImGui::SliderFloat("FOV (deg)", &fov, 30.0f, 120.0f, "%.0f"))
        renderer->setCameraFov(fov);
    float nearClip = env.nearClip;
    float farClip = env.farClip;
    bool clipChanged = ImGui::DragFloat("near", &nearClip, 0.01f, 0.01f, 10.0f);
    clipChanged |= ImGui::DragFloat("far", &farClip, 1.0f, 1.0f, 10000.0f);
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
                  "stylize = %s\n"
                  "stylize_shadows = %s\n"
                  "stylize_highlights = %s\n"
                  "shadow_strength = %.2f\n"
                  "highlight_strength = %.2f\n"
                  "shadow_color_srgb = [%.4f, %.4f, %.4f]\n"
                  "highlight_color_srgb = [%.4f, %.4f, %.4f]\n"
                  "outline_thickness = %.1f\n"
                  "shadow_threshold = %.2f\n"
                  "highlight_threshold = %.2f\n"
                  "highlight_dark_fade = %.2f\n"
                  "outline_enabled = %s\n"
                  "outline_color_srgb = [%.4f, %.4f, %.4f]\n"
                  "outline_opacity = %.2f\n"
                  "outline_depth_sens = %.1f\n"
                  "outline_normal_sens = %.2f\n"
                  "outline_sharpness = %.2f\n"
                  "outline_dist_fade = %.3f\n"
                  "outline_dark_fade = %.2f\n"
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
                  env.stylize ? "true" : "false",
                  stylizeShadows ? "true" : "false",
                  stylizeHighlights ? "true" : "false", shadowStrength,
                  highlightStrength, shadowColor.x, shadowColor.y, shadowColor.z,
                  highlightColor.x, highlightColor.y, highlightColor.z,
                  outlineThickness, shadowThreshold, highlightThreshold,
                  highlightDarkFade,
                  outlineEnabled ? "true" : "false",
                  outlineColor.x, outlineColor.y, outlineColor.z,
                  outlineOpacity, outlineDepthSens, outlineNormalSens,
                  outlineSharpness, outlineDistFade, outlineDarkFade,
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
