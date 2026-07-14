#include "Platform.h"

#include <eng/Log.h>

#include <SDL2/SDL_syswm.h>

namespace eng {

bool Platform::init(const std::string& title, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        log::error("Platform: SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    mWindow = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, width, height,
                               SDL_WINDOW_RESIZABLE);
    if (!mWindow) {
        log::error("Platform: SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(mWindow, &wmInfo);
#if defined(SDL_VIDEO_DRIVER_X11)
    mNativeHandle = static_cast<uintptr_t>(wmInfo.info.x11.window);
#endif
    if (!mNativeHandle) {
        log::error("Platform: no X11 window handle (run with SDL_VIDEODRIVER=x11)");
        return false;
    }
    return true;
}

void Platform::shutdown()
{
    if (mWindow) {
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
    }
    SDL_Quit();
}

} // namespace eng
