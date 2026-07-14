#pragma once
#include <eng/Input.h>
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

    const std::vector<SDL_Keycode>& find(const std::string& action) const
    {
        auto it = bindings.find(action);
        if (it == bindings.end())
            log::fatal("Input: unbound action '%s'", action.c_str());
        return it->second;
    }
};

} // namespace eng
