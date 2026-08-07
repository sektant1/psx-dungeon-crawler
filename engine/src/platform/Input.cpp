#include <eng/Input.h>

#include <eng/Config.h>

#include "InputImpl.h"

namespace eng {

Input::Input() : mImpl(new Impl) {}
Input::~Input() = default;

bool Input::loadBindings(const Config& cfg)
{
    std::map<std::string, std::vector<SDL_Keycode>> tmp;
    for (auto&& [action, keyNames] : cfg.bindings()) {
        for (const std::string& keyName : keyNames) {
            SDL_Keycode kc = SDL_GetKeyFromName(keyName.c_str());
            if (kc == SDLK_UNKNOWN) {
                log::error("Input: unknown key name '%s' for action '%s' "
                           "(use SDL key names, e.g. \"W\", \"Space\", "
                           "\"Return\", \"Escape\", \"Left Shift\")",
                           keyName.c_str(), action.c_str());
                return false; // tmp discarded; existing bindings unchanged
            }
            tmp[action].push_back(kc);
        }
    }
    mImpl->bindings = std::move(tmp);
    mImpl->down.clear();
    mImpl->pressed.clear();
    return true;
}

bool Input::rebind(const std::string& action, const std::string& keyName)
{
    SDL_Keycode kc = SDL_GetKeyFromName(keyName.c_str());
    if (kc == SDLK_UNKNOWN) {
        log::error("Input: unknown key name '%s' for rebind of '%s'",
                   keyName.c_str(), action.c_str());
        return false;
    }
    mImpl->bindings[action] = { kc };
    return true;
}

bool Input::isDown(const std::string& action) const
{
    for (SDL_Keycode kc : mImpl->find(action))
        if (mImpl->down.count(kc))
            return true;
    return false;
}

bool Input::wasPressed(const std::string& action) const
{
    for (SDL_Keycode kc : mImpl->find(action))
        if (mImpl->pressed.count(kc))
            return true;
    return false;
}

namespace {
Uint8 toSdlButton(MouseButton button)
{
    switch (button) {
    case MouseButton::Left: return SDL_BUTTON_LEFT;
    case MouseButton::Right: return SDL_BUTTON_RIGHT;
    case MouseButton::Middle: return SDL_BUTTON_MIDDLE;
    }
    return SDL_BUTTON_LEFT;
}
} // namespace

bool Input::wasMouseClicked() const
{
    return wasMousePressed(MouseButton::Left);
}

bool Input::isMouseDown(MouseButton button) const
{
    return mImpl->mouseDown.count(toSdlButton(button)) != 0;
}

bool Input::wasMousePressed(MouseButton button) const
{
    return mImpl->mousePressed.count(toSdlButton(button)) != 0;
}
glm::vec2 Input::mouseDelta() const { return mImpl->delta; }
float Input::wheelDelta() const { return mImpl->wheel; }

void Input::setMouseGrab(bool grab)
{
    // Callers set this every frame from a boolean they already hold, so only a
    // real transition matters here.
    const bool was = SDL_GetRelativeMouseMode() == SDL_TRUE;
    if (SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE) != 0) {
        log::error("Input: SDL_SetRelativeMouseMode(%d) failed: %s",
                   int(grab), SDL_GetError());
        return;
    }
    if (grab && !was) {
        // Entering relative mode warps the pointer to the centre and SDL
        // reports the warp as motion. Drop SDL's own accumulator and the next
        // tick's events, or that jump becomes a view snap.
        SDL_GetRelativeMouseState(nullptr, nullptr);
        mImpl->discardNextMotion();
    }
}

bool Input::mouseGrabbed() const
{
    return SDL_GetRelativeMouseMode() == SDL_TRUE;
}

} // namespace eng
