#include "Platform.h"

#include <eng/Log.h>

#include <SDL2/SDL_syswm.h>

namespace eng {

bool Platform::init(const std::string& title, int width, int height)
{
    // Ogre's GL3Plus render window is created against an X11 handle, so SDL
    // must run on X11 (XWayland). Under a native-Wayland session the x11
    // union member of SDL_SysWMinfo aliases the wl_surface pointer, which
    // Ogre rejects ("Invalid parentWindowHandle") before the first frame.
    // Respect an explicit user override; otherwise pin the driver.
    SDL_setenv("SDL_VIDEODRIVER", "x11", 0 /* no overwrite */);
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
    if (!SDL_GetWindowWMInfo(mWindow, &wmInfo)) {
        log::error("Platform: SDL_GetWindowWMInfo failed: %s", SDL_GetError());
        return false;
    }
    // The union is only meaningful for the subsystem SDL actually picked;
    // reading .x11 under Wayland yields a garbage pointer, not a window ID.
    if (wmInfo.subsystem != SDL_SYSWM_X11) {
        log::error("Platform: SDL video driver is '%s', need X11 "
                   "(run with SDL_VIDEODRIVER=x11)",
                   SDL_GetCurrentVideoDriver());
        return false;
    }
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
    if (!mWindow)
        return;
    SDL_DestroyWindow(mWindow);
    mWindow = nullptr;
    SDL_Quit();
}

} // namespace eng
