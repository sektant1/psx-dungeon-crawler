#pragma once
#include <SDL2/SDL.h>

#include <cstdint>
#include <string>

namespace eng {

// Internal SDL window wrapper. The selected renderer determines whether this
// is an external native window for OGRE or an SDL Vulkan surface.
class Platform
{
public:
    bool init(const std::string& title, int width, int height);
    void shutdown(); // call AFTER RenderCore::shutdown (Ogre holds the handle)
    uintptr_t nativeHandle() const { return mNativeHandle; }
    SDL_Window* window() const { return mWindow; } // for the imgui SDL2 backend

private:
    SDL_Window* mWindow = nullptr;
    uintptr_t mNativeHandle = 0;
};

} // namespace eng
