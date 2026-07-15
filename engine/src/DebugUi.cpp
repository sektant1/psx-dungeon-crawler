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
    if (ImGui::SliderFloat("vertex snap", &precisionMultiplier, 0.0f, 1.0f))
        renderer->setGlobalMaterialParam("precisionMultiplier",
                                         precisionMultiplier);

    const EnvState& env = renderer->envState();
    bool dither = env.dither;
    if (ImGui::Checkbox("dither pass", &dither))
        renderer->setDitherEnabled(dither);
    if (ImGui::SliderFloat("colour depth", &colDepth, 1.0f, 64.0f, "%.0f"))
        renderer->setMaterialParam("PSX/DitherPost", "colDepth", colDepth);
    if (ImGui::Checkbox("dither banding", &ditherBanding))
        renderer->setMaterialParam("PSX/DitherPost", "ditherBanding",
                                   ditherBanding ? 1.0f : 0.0f);
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
    char buf[2048];
    std::snprintf(buf, sizeof(buf),
                  "[debug_tuning]\n"
                  "precision_multiplier = %.4f\n"
                  "dither = %s\n"
                  "col_depth = %.1f\n"
                  "dither_banding = %s\n"
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
                  "ambient_linear = [%.4f, %.4f, %.4f]\n"
                  "fog_colour_linear = [%.4f, %.4f, %.4f]\n"
                  "fog_density = %.4f\n"
                  "background_srgb = [%.4f, %.4f, %.4f]\n"
                  "fov_deg = %.1f\n"
                  "near_clip = %.3f\n"
                  "far_clip = %.1f\n",
                  precisionMultiplier, env.dither ? "true" : "false", colDepth,
                  ditherBanding ? "true" : "false", ditherDarkFade,
                  env.pixelSize, env.perPixelLighting ? "true" : "false",
                  env.omniAttenuation,
                  env.bloom ? "true" : "false", env.bloomThreshold,
                  env.bloomIntensity,
                  env.stylize ? "true" : "false",
                  stylizeShadows ? "true" : "false",
                  stylizeHighlights ? "true" : "false", shadowStrength,
                  highlightStrength, shadowColor.x, shadowColor.y, shadowColor.z,
                  highlightColor.x, highlightColor.y, highlightColor.z,
                  env.ambient.x, env.ambient.y,
                  env.ambient.z, env.fogColour.x, env.fogColour.y,
                  env.fogColour.z, env.fogDensity, env.background.x,
                  env.background.y, env.background.z, env.fovDeg, env.nearClip,
                  env.farClip);
    SDL_SetClipboardText(buf);
    log::info("DebugUi: copied to clipboard:\n%s", buf);
}

} // namespace eng
