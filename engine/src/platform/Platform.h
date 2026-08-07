#pragma once
#include <SDL2/SDL.h>

#include <cstdint>
#include <string>

namespace eng {

// Internal SDL window wrapper. The window carries SDL_WINDOW_VULKAN and the
// backend builds its surface from it, so no native handle is ever extracted.
class Platform
{
public:
    bool init(const std::string& title, int width, int height);
    void shutdown(); // call AFTER RenderCore::shutdown, which uses the window
    SDL_Window* window() const { return mWindow; } // for the imgui SDL2 backend

private:
    SDL_Window* mWindow = nullptr;
};

} // namespace eng
