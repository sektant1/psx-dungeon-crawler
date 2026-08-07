#include "Platform.h"

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>

#include <filesystem>

#include <cstdlib>

#include <stb_image.h>

namespace eng {

namespace {

void setWindowIcon(SDL_Window* window)
{
    // Content first, so a project ships its own icon by putting a file at
    // ui/icon.png -- the project's pack is mounted over the engine's, so that
    // shadows whatever the engine provides with no code and no setting.
    //
    // The repo's own avatar is the fallback, and only reachable in a source
    // tree: an exported build has no docs/ and used to warn about it on every
    // launch, which is a shipped game complaining that it is not this one.
    std::filesystem::path path = assets::resolve("ui/icon.png");
    if (path.empty())
        path = assets::project() / "docs/media/avatar.png";

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height,
                                &channels, STBI_rgb_alpha);
    if (!pixels) {
        // Not a warning: a game with no icon is a game with no icon. The window
        // still opens, and the platform default is a perfectly good icon.
        log::info("Platform: no window icon (%s)", path.string().c_str());
        return;
    }

    SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels, width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
    if (!icon) {
        log::warn("Platform: cannot create window icon surface: %s",
                  SDL_GetError());
        stbi_image_free(pixels);
        return;
    }

    SDL_SetWindowIcon(window, icon);
    SDL_FreeSurface(icon);
    stbi_image_free(pixels);
}

} // namespace

bool Platform::init(const std::string& title, int width, int height)
{
    // Stable window class/app-id so tiling compositors (Hyprland) can target the
    // window with a float rule instead of tiling it (which resizes the render
    // surface). Set before SDL_Init; honours a user override. X11 name is
    // "<instance> <class>"; the class (2nd token) is what Hyprland matches.
    SDL_setenv("SDL_VIDEO_X11_WMCLASS", "raven-engine", 0);
    SDL_setenv("SDL_VIDEO_WAYLAND_WMCLASS", "raven-engine", 0);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        log::error("Platform: SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                   (std::getenv("RAVEN_FULLSCREEN")
                        ? SDL_WINDOW_FULLSCREEN_DESKTOP
                        : 0) |
                   SDL_WINDOW_VULKAN;
    mWindow = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, width, height,
                               flags);
    if (!mWindow) {
        log::error("Platform: SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    setWindowIcon(mWindow);
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
