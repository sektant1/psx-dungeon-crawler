#include <eng/Engine.h>

#include <eng/Log.h>

#include "InputImpl.h"
#include "Platform.h"
#include "RenderCore.h"
#include "RenderPresets.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <vector>

// detail::coreOf / detail::registerRoot come from eng/Renderer.h (via
// Engine.h); their definitions live in Renderer.cpp next to Renderer::Impl.
namespace eng {

struct Engine::Impl {
    Platform platform;
    std::chrono::steady_clock::time_point prev;
    bool hasPrev = false;
    std::string screenshotPath;
    int frameCount = 0;
    int screenshotFrame = 90;
    int benchmarkFrames = 0;
    std::vector<float> frameSamples;
    // Frame limiter: minimum seconds per frame (0 = uncapped). Paces the loop
    // when vsync is off so the GPU isn't driven flat out.
    float minFrameSec = 0.0f;
    // Deterministic capture: when set, tick() returns this fixed dt instead of
    // wall-clock time, so animTime/physics/particles advance identically every
    // run and PSX_SCREENSHOT becomes a real pixel-diff regression oracle.
    float fixedTickDt = 0.0f; // 0 = disabled (normal wall-clock timing)
};

Engine::Engine() : mImpl(new Impl) {}
Engine::~Engine() { shutdown(); } // defensive; shutdown() is idempotent

System* Engine::registerSystem(System::StrongPtr sys) {
    sys->initialize();
    System* raw = sys.get();
    mSystems.push_back(std::move(sys));
    return raw;
}

void Engine::updateSystems(float dt) {
    for (auto& s : mSystems) s->update(dt);
}

bool Engine::init(const std::string& configPath, const std::string& appAssetDir)
{
    if (!mConfig.load(configPath))
        return false;
    const std::string title = mConfig.getString("window.title", "eng");
    const int width = static_cast<int>(mConfig.getNumber("window.width", 960));
    const int height = static_cast<int>(mConfig.getNumber("window.height", 720));
    const bool vsync = mConfig.getBool("window.vsync", false);
    if (mConfig.getBool("window.limit_fps", false)) {
        const double fps = mConfig.getNumber("window.max_fps", 60.0);
        if (fps > 0.0)
            mImpl->minFrameSec = float(1.0 / fps);
    }

    if (!mImpl->platform.init(title, width, height))
        return false;
    if (!detail::coreOf(mRenderer).init(mImpl->platform.nativeHandle(),
                                        mImpl->platform.window(), width, height,
                                        title, appAssetDir, vsync)) {
        shutdown();
        return false;
    }
    detail::registerRoot(mRenderer);
    if (const char* presetName = std::getenv("PSX_RENDER_PRESET")) {
        int id = renderPresetFromName(presetName);
        if (id > 0)
            applyRenderPreset(mRenderer, renderPresetValues(id));
        else
            log::warn("Unknown PSX_RENDER_PRESET '%s'", presetName);
    }
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
    // Screenshot capture drives a fixed timestep so the frame is reproducible
    // (default 1/60 s). PSX_FIXED_DT overrides it (e.g. to land on a specific
    // animation phase); set it explicitly to force deterministic timing without
    // capturing.
    if (shot)
        mImpl->fixedTickDt = 1.0f / 60.0f;
    if (const char* fdt = std::getenv("PSX_FIXED_DT")) {
        const float v = float(std::atof(fdt));
        if (v > 0.0f)
            mImpl->fixedTickDt = v;
    }
    if (const char* frame = std::getenv("PSX_SCREENSHOT_FRAME"))
        mImpl->screenshotFrame = std::max(1, std::atoi(frame));
    if (const char* frames = std::getenv("PSX_BENCH_FRAMES")) {
        mImpl->benchmarkFrames = std::max(1, std::atoi(frames));
        mImpl->frameSamples.reserve(size_t(mImpl->benchmarkFrames));
    }
    // Deterministic capture: Ogre's ParticleFX emitters draw from C rand(),
    // which some init path reseeds from wall-clock time. Pin it to a constant
    // so fire/ash/spark emission is identical every run (seed here, before any
    // particle spawns in the game's level build).
    if (mImpl->fixedTickDt > 0.0f)
        std::srand(1234u);

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
                   e.key.keysym.sym == SDLK_F2) {
            mRenderer.setWireframeDebug(!mRenderer.envState().wireframe);
        } else {
            mInput.mImpl->onEvent(e);
        }
        // Mirror every event into imgui's SDL2 backend (mouse, wheel, keys,
        // text, cursor). Cheap when no UI is shown; gives the debug console
        // correct mouse/scroll/cursor when it is open.
        ImGui_ImplSDL2_ProcessEvent(&e);
    }
    // Deterministic capture: ignore wall-clock and advance by a fixed step so
    // every frame is reproducible. Skips the frame limiter (no pacing needed).
    if (mImpl->fixedTickDt > 0.0f)
        return mImpl->fixedTickDt;

    auto now = std::chrono::steady_clock::now();
    if (!mImpl->hasPrev) {
        mImpl->prev = now;
        mImpl->hasPrev = true;
        return 0.0f;
    }
    // Frame limiter: block until this frame has taken at least minFrameSec.
    if (mImpl->minFrameSec > 0.0f) {
        const auto target =
            mImpl->prev + std::chrono::duration_cast<
                              std::chrono::steady_clock::duration>(
                              std::chrono::duration<float>(mImpl->minFrameSec));
        if (now < target) {
            std::this_thread::sleep_until(target);
            now = std::chrono::steady_clock::now();
        }
    }
    const float dt = std::chrono::duration<float>(now - mImpl->prev).count();
    mImpl->prev = now;
    return std::min(dt, 0.1f);
}

void Engine::beginImGuiFrame(float dt)
{
    detail::coreOf(mRenderer).beginImGuiFrame(dt);
}

bool Engine::imguiReady() const
{
    return detail::coreOf(const_cast<Renderer&>(mRenderer)).imguiReady();
}

bool Engine::imguiWantsMouse() const
{
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
}

bool Engine::imguiWantsKeyboard() const
{
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
}

void Engine::renderFrame(float dt, float animDt)
{
    // animDt drives Ogre's particle + animation advance; dt stays wall time for
    // the benchmark/screenshot hooks below. animDt<0 means "same as dt".
    const float adt = (animDt < 0.0f) ? dt : animDt;
    mRenderer.updateParticles(adt); // recycle finished one-shot particle systems
    detail::coreOf(mRenderer).renderFrame(adt);
    // Headless-friendly performance regression hook. Skip the first 60 frames
    // so shader/texture warm-up cannot masquerade as steady-state spikes.
    if (mImpl->benchmarkFrames > 0 && mImpl->frameCount >= 60 && dt > 0.0f) {
        mImpl->frameSamples.push_back(dt * 1000.0f);
        if (int(mImpl->frameSamples.size()) == mImpl->benchmarkFrames) {
            std::vector<float> sorted = mImpl->frameSamples;
            std::sort(sorted.begin(), sorted.end());
            const auto percentile = [&](float p) {
                const size_t i = size_t(p * float(sorted.size() - 1));
                return sorted[i];
            };
            log::info("FrameStats: n=%zu p50=%.3fms p95=%.3fms p99=%.3fms max=%.3fms",
                      sorted.size(), percentile(0.50f), percentile(0.95f),
                      percentile(0.99f), sorted.back());
            mClose = true;
        }
    }
    ++mImpl->frameCount;
    if (!mImpl->screenshotPath.empty() &&
        mImpl->frameCount == mImpl->screenshotFrame) {
        detail::coreOf(mRenderer).writeScreenshot(mImpl->screenshotPath);
        mClose = true;
    }
}

void Engine::shutdown()
{
    for (auto it = mSystems.rbegin(); it != mSystems.rend(); ++it)
        (*it)->terminate();
    mSystems.clear();
    detail::coreOf(mRenderer).shutdown(); // Ogre first
    mImpl->platform.shutdown();           // native window after
}

} // namespace eng
