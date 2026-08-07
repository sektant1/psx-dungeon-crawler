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
    std::set<Uint8> mouseDown;
    std::set<Uint8> mousePressed;
    glm::vec2 delta{0.0f};
    float wheel = 0.0f;
    // Ticks of mouse motion still to be thrown away.
    //
    // Entering relative mode warps the pointer to the window centre, and SDL
    // reports that warp as an ordinary SDL_MOUSEMOTION whose xrel/yrel is the
    // whole jump from wherever the pointer happened to be. Fed to mouse look
    // that is a view snap of arbitrary size -- which is why the deterministic
    // capture intermittently came back staring at the ceiling, and why
    // alt-tabbing back into the game could fling the camera. Same on focus
    // gain, for the same reason.
    //
    // Counted in ticks rather than events because the warp can arrive split
    // across more than one motion event.
    int discardMotionTicks = 0;

    void beginTick()
    {
        pressed.clear();
        mousePressed.clear();
        delta = glm::vec2(0.0f);
        wheel = 0.0f;
        if (discardMotionTicks > 0)
            --discardMotionTicks;
    }

    // Drop the motion arriving in the next pump. beginTick() decrements before
    // events are polled, so 2 here discards exactly the following tick.
    void discardNextMotion() { discardMotionTicks = 2; }

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
            if (discardMotionTicks > 0)
                break; // a warp, not the player moving the mouse
            delta += glm::vec2(float(e.motion.xrel), float(e.motion.yrel));
            break;
        case SDL_WINDOWEVENT:
            // Regaining focus re-warps the pointer; the motion that follows is
            // the warp, not input.
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                discardNextMotion();
            break;
        case SDL_MOUSEBUTTONDOWN:
            mouseDown.insert(e.button.button);
            mousePressed.insert(e.button.button);
            break;
        case SDL_MOUSEBUTTONUP:
            mouseDown.erase(e.button.button);
            break;
        case SDL_MOUSEWHEEL: {
            // preciseY carries trackpad/high-resolution wheels; on a notched
            // mouse it is exactly the integer y. The flip flag is folded in
            // here so no caller has to know the platform convention.
#if SDL_VERSION_ATLEAST(2, 0, 18)
            const float notches = e.wheel.preciseY;
#else
            const float notches = float(e.wheel.y);
#endif
            wheel += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -notches
                                                                 : notches;
            break;
        }
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
