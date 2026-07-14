#include <eng/Engine.h>

#include <eng/Log.h>

#include "InputImpl.h"
#include "Platform.h"
#include "RenderCore.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

// detail::coreOf / detail::registerRoot come from eng/Renderer.h (via
// Engine.h); their definitions live in Renderer.cpp next to Renderer::Impl.
namespace eng {

struct Engine::Impl {
    Platform platform;
    std::chrono::steady_clock::time_point prev;
    std::string screenshotPath;
    int frameCount = 0;
};

Engine::Engine() : mImpl(new Impl) {}
Engine::~Engine() = default;

bool Engine::init(const std::string& configPath, const std::string& appAssetDir)
{
    if (!mConfig.load(configPath))
        return false;
    const std::string title = mConfig.getString("window.title", "eng");
    const int width = static_cast<int>(mConfig.getNumber("window.width", 960));
    const int height = static_cast<int>(mConfig.getNumber("window.height", 720));

    if (!mImpl->platform.init(title, width, height))
        return false;
    if (!detail::coreOf(mRenderer).init(mImpl->platform.nativeHandle(), width,
                                        height, title, appAssetDir))
        return false;
    detail::registerRoot(mRenderer);
    if (!mInput.loadBindings(mConfig))
        return false;

    const char* shot = std::getenv("PSX_SCREENSHOT");
    if (shot)
        mImpl->screenshotPath = shot;
    mImpl->prev = std::chrono::steady_clock::now();
    return true;
}

float Engine::tick()
{
    mInput.mImpl->beginTick();
    for (SDL_Event e; SDL_PollEvent(&e);) {
        if (e.type == SDL_QUIT)
            mClose = true;
        else if (e.type == SDL_WINDOWEVENT &&
                 e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            detail::coreOf(mRenderer).onResize(e.window.data1, e.window.data2);
        else
            mInput.mImpl->onEvent(e);
    }
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - mImpl->prev).count();
    mImpl->prev = now;
    return std::min(dt, 0.1f);
}

void Engine::renderFrame(float dt)
{
    detail::coreOf(mRenderer).renderFrame(dt);
    if (!mImpl->screenshotPath.empty() && ++mImpl->frameCount == 90) {
        detail::coreOf(mRenderer).writeScreenshot(mImpl->screenshotPath);
        mClose = true;
    }
}

void Engine::shutdown()
{
    detail::coreOf(mRenderer).shutdown(); // Ogre first
    mImpl->platform.shutdown();           // native window after
}

} // namespace eng
