#include <eng/Input.h>

#include <eng/Config.h>
#include <eng/Log.h>

#include <SDL2/SDL.h>

#include <map>
#include <set>
#include <vector>

namespace eng {

struct Input::Impl {
    std::map<std::string, std::vector<SDL_Keycode>> bindings;
    std::set<SDL_Keycode> down;
    std::set<SDL_Keycode> pressed;
    bool mouseClicked = false;
    bool grabbed = false;
    glm::vec2 delta{0.0f};

    void beginTick()
    {
        pressed.clear();
        mouseClicked = false;
        delta = glm::vec2(0.0f);
    }

    void onEvent(const SDL_Event& e)
    {
        switch (e.type) {
        case SDL_KEYDOWN:
            if (e.key.repeat == 0) {
                down.insert(e.key.keysym.sym);
                pressed.insert(e.key.keysym.sym);
            }
            break;
        case SDL_KEYUP:
            down.erase(e.key.keysym.sym);
            break;
        case SDL_MOUSEMOTION:
            delta += glm::vec2(float(e.motion.xrel), float(e.motion.yrel));
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT)
                mouseClicked = true;
            break;
        }
    }

    const std::vector<SDL_Keycode>* find(const std::string& action) const
    {
        auto it = bindings.find(action);
        if (it == bindings.end())
            log::fatal("Input: unbound action '%s'", action.c_str());
        return &it->second;
    }
};

Input::Input() : mImpl(new Impl) {}
Input::~Input() = default;

bool Input::loadBindings(const Config& cfg)
{
    mImpl->bindings.clear();
    for (auto&& [action, keyNames] : cfg.bindings()) {
        for (const std::string& keyName : keyNames) {
            SDL_Keycode kc = SDL_GetKeyFromName(keyName.c_str());
            if (kc == SDLK_UNKNOWN) {
                log::error("Input: unknown key name '%s' for action '%s' "
                           "(use SDL key names, e.g. \"W\", \"Space\", "
                           "\"Return\", \"Escape\", \"Left Shift\")",
                           keyName.c_str(), action.c_str());
                return false;
            }
            mImpl->bindings[action].push_back(kc);
        }
    }
    return true;
}

bool Input::isDown(const std::string& action) const
{
    for (SDL_Keycode kc : *mImpl->find(action))
        if (mImpl->down.count(kc))
            return true;
    return false;
}

bool Input::wasPressed(const std::string& action) const
{
    for (SDL_Keycode kc : *mImpl->find(action))
        if (mImpl->pressed.count(kc))
            return true;
    return false;
}

bool Input::wasMouseClicked() const { return mImpl->mouseClicked; }
glm::vec2 Input::mouseDelta() const { return mImpl->delta; }

void Input::setMouseGrab(bool grab)
{
    SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE);
    mImpl->grabbed = grab;
}

bool Input::mouseGrabbed() const { return mImpl->grabbed; }

} // namespace eng
