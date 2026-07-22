#include "UiContext.h"

#include "RenderCore.h"

#include <OgreImGuiOverlay.h>
#include <OgreRenderWindow.h>

#include <imgui.h>

#include <cstddef>

namespace eng::ui {

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

} // namespace

void Context::init(RenderCore* core) { mCore = core; }

bool Context::onEvent(const SDL_Event& e)
{
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

void Context::beginFrame(float dt)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(mCore->window()->getWidth()),
                            float(mCore->window()->getHeight()));
    Ogre::ImGuiOverlay::NewFrame();
    if (!mCore->imguiOverlay()->isVisible())
        mCore->imguiOverlay()->show(); // safe now: NewFrame has run

    mFrameMs[size_t(mFrameMsIdx)] = dt * 1000.0f;
    mFrameMsIdx = (mFrameMsIdx + 1) % int(mFrameMs.size());
}

void Context::setHudPrompt(const std::string& text) { mHudPrompt = text; }

void Context::drawHudPrompt()
{
    // HUD prompt: bottom-centre, borderless, always on top of the scene.
    if (mHudPrompt.empty())
        return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const ImVec2 ts = ImGui::CalcTextSize(mHudPrompt.c_str());
    ImGui::SetNextWindowPos(ImVec2((ds.x - ts.x) * 0.5f, ds.y * 0.78f));
    ImGui::SetNextWindowBgAlpha(0.45f);
    ImGui::Begin("##hudprompt", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(mHudPrompt.c_str());
    ImGui::End();
}

float Context::avgFrameMs() const
{
    // FPS from the engine-side frame-time ring: window lastFPS needs a
    // second of frames before it primes (reads 0.0 in short captures).
    float accMs = 0.0f;
    for (float ms : mFrameMs)
        accMs += ms;
    return accMs / float(mFrameMs.size());
}

} // namespace eng::ui
