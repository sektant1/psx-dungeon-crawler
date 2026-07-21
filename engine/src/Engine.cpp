#include <eng/Engine.h>

#include <eng/Log.h>

#include "DebugUiImpl.h"
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
    bool hasPrev = false;
    std::string screenshotPath;
    int frameCount = 0;
    bool grabBeforeDebugUi = false;
};

Engine::Engine() : mImpl(new Impl) {}
Engine::~Engine() { shutdown(); } // defensive; shutdown() is idempotent

bool Engine::init(const std::string& configPath, const std::string& appAssetDir)
{
    if (!mConfig.load(configPath))
        return false;
    const std::string title = mConfig.getString("window.title", "eng");
    const int width = static_cast<int>(mConfig.getNumber("window.width", 960));
    const int height = static_cast<int>(mConfig.getNumber("window.height", 720));
    const bool vsync = mConfig.getBool("window.vsync", false);

    if (!mImpl->platform.init(title, width, height))
        return false;
    if (!detail::coreOf(mRenderer).init(mImpl->platform.nativeHandle(), width,
                                        height, title, appAssetDir, vsync)) {
        shutdown();
        return false;
    }
    detail::registerRoot(mRenderer);
    mDebugUi.mImpl->init(&detail::coreOf(mRenderer), &mRenderer);
    if (std::getenv("PSX_DEBUG_UI"))
        mDebugUi.setVisible(true);
    // Safe before any attachMesh: entities created later join the debug
    // view through the attachMesh wireframe hook.
    if (std::getenv("PSX_WIREFRAME"))
        mRenderer.setWireframeDebug(true);
    if (!mInput.loadBindings(mConfig)) {
        shutdown();
        return false;
    }

    const char* shot = std::getenv("PSX_SCREENSHOT");
    if (shot)
        mImpl->screenshotPath = shot;
    mImpl->hasPrev = false;
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
        else if (e.type == SDL_WINDOWEVENT &&
                 e.window.event == SDL_WINDOWEVENT_CLOSE)
            mClose = true;
        else if (e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
                 e.key.keysym.sym == SDLK_F1) {
            const bool show = !mDebugUi.visible();
            if (show) {
                mImpl->grabBeforeDebugUi = mInput.mouseGrabbed();
                mInput.setMouseGrab(false);
            } else {
                mInput.setMouseGrab(mImpl->grabBeforeDebugUi);
            }
            mDebugUi.setVisible(show);
        } else if (e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
                   e.key.keysym.sym == SDLK_F2) {
            mRenderer.setWireframeDebug(!mRenderer.envState().wireframe);
        } else {
            const bool consumed = mDebugUi.mImpl->onEvent(e);
            // KEYUP always reaches Input (no stuck keys); everything else
            // stops here when ImGui captured it.
            if (!consumed || e.type == SDL_KEYUP)
                mInput.mImpl->onEvent(e);
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (!mImpl->hasPrev) {
        mImpl->prev = now;
        mImpl->hasPrev = true;
        return 0.0f;
    }
    const float dt = std::chrono::duration<float>(now - mImpl->prev).count();
    mImpl->prev = now;
    return std::min(dt, 0.1f);
}

void Engine::renderFrame(float dt)
{
    mDebugUi.mImpl->buildFrame(dt);
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
