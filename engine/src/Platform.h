#pragma once
#include <SDL2/SDL.h>

#include <cstdint>
#include <string>

namespace eng {

// Internal SDL window wrapper. No SDL_WINDOW_OPENGL: Ogre GL3Plus creates
// its own GL context against the raw native handle.
class Platform
{
public:
    bool init(const std::string& title, int width, int height);
    void shutdown(); // call AFTER RenderCore::shutdown (Ogre holds the handle)
    uintptr_t nativeHandle() const { return mNativeHandle; }

private:
    SDL_Window* mWindow = nullptr;
    uintptr_t mNativeHandle = 0;
};

} // namespace eng
