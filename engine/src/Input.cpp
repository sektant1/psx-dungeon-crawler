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

bool Input::wasMouseClicked() const { return mImpl->mouseClicked; }
glm::vec2 Input::mouseDelta() const { return mImpl->delta; }

void Input::setMouseGrab(bool grab)
{
    if (SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE) != 0)
        log::error("Input: SDL_SetRelativeMouseMode(%d) failed: %s",
                   int(grab), SDL_GetError());
}

bool Input::mouseGrabbed() const
{
    return SDL_GetRelativeMouseMode() == SDL_TRUE;
}

} // namespace eng
