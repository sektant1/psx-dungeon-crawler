#include <eng/Engine.h>

#include <eng/Log.h>
#include <eng/MemoryProfiler.h>
#include <eng/telemetry/RedisSink.h>
#include <eng/telemetry/Telemetry.h>
#include <eng/assets/AssetRoot.h>
#include <eng/render/FrameCapture.h>
#include <eng/render/GifRecorder.h>

#include "platform/InputImpl.h"
#include "platform/Platform.h"
#if defined(ENG_RENDERER_RHI)
#include "render/rhi/RenderCore.h"
#else
#include "render/RenderCore.h"
#endif
#include "render/RenderPresets.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <vector>

// detail::coreOf / detail::registerRoot come from eng/Renderer.h (via
// Engine.h); their definitions live in Renderer.cpp next to Renderer::Impl.
// Set by the build (1 in Debug, 0 otherwise). Defined here too so this file
// still compiles in a tree that predates the option.
#ifndef ENG_CONNECTOR_DEFAULT
#    define ENG_CONNECTOR_DEFAULT 0
#endif

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
    FrameCapture frameCapture;
    GifRecorder recorder;
    // Frame limiter: minimum seconds per frame (0 = uncapped). Paces the loop
    // when vsync is off so the GPU isn't driven flat out.
    float minFrameSec = 0.0f;
    // Deterministic capture: when set, tick() returns this fixed dt instead of
    // wall-clock time, so animTime/physics/particles advance identically every
    // run and RAVEN_SCREENSHOT becomes a real pixel-diff regression oracle.
    float fixedTickDt = 0.0f; // 0 = disabled (normal wall-clock timing)
    bool loading = false;     // see Engine::setLoadingPhase
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

bool Engine::init(const std::string& configPath, const std::string& mountSet,
                  int renderPreset, const std::string& projectDir)
{
    // The content root comes up first: everything below -- the config file, the
    // renderer's resource locations, the font, the hint table -- is resolved
    // through it, so a failure here has to stop the run rather than surface
    // later as a window full of missing textures.
    if (!assets::init()) {
        log::error("Engine: no content root; cannot start");
        return false;
    }
    if (!assets::mount(mountSet)) {
        log::error("Engine: cannot mount content set '%s'", mountSet.c_str());
        return false;
    }
    // The conditioned pack, if `raven_acp build` has produced one. Optional by
    // design: a source checkout with no pack runs off the source loaders, which
    // is what makes the pipeline something a project adopts rather than
    // something it must have before it can start.
    assets::mountCooked();

    // A project's own content goes on last and therefore resolves first, which
    // is what lets the line below find a config the project ships rather than
    // the engine's. Before this point there is nothing to overlay it onto; any
    // earlier and assets::init() above would have wiped it.
    if (!projectDir.empty() && !assets::mountProject(projectDir)) {
        log::error("Engine: cannot mount project '%s'", projectDir.c_str());
        return false;
    }

    const std::string configFile = assets::resolve(configPath).string();
    if (configFile.empty()) {
        log::error("Engine: config '%s' is not in any mounted pack",
                   configPath.c_str());
        return false;
    }
    if (!mConfig.load(configFile))
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
                                        title, vsync)) {
        shutdown();
        return false;
    }
    // Call-stack sampling for the heap. The exact counters always run (they are
    // two atomic adds); this only sets how often an allocation also pays for a
    // backtrace. RAVEN_MEMPROF=0 turns capture off and leaves the counters.
    if (const char* rate = std::getenv("RAVEN_MEMPROF")) {
        memprof::setSampleRate(
            static_cast<std::uint32_t>(std::strtoul(rate, nullptr, 10)));
        log::info("memprof: call-stack sampling 1-in-%u",
                  memprof::stats().sampleRate);
    }

    // Debug telemetry. A Debug build attaches by default -- a debug channel you
    // have to remember to turn on is a debug channel that is off on the run
    // where the bug appeared -- and a Release build stays opt-in, so a build to
    // measure opens no socket and starts no thread.
    //
    //   RAVEN_CONNECTOR=1            defaults
    //   RAVEN_CONNECTOR=host:port    somewhere else
    //   RAVEN_CONNECTOR=0            off, whatever the build says
    const char* connector = std::getenv("RAVEN_CONNECTOR");
    std::string spec = connector ? connector : "";
    const bool wantConnector =
        connector ? (spec != "0" && spec != "off" && !spec.empty())
                  : ENG_CONNECTOR_DEFAULT != 0;
    if (wantConnector) {
        telemetry::RedisConfig redis;
        if (spec != "1" && spec != "on" && !spec.empty()) {
            const std::size_t colon = spec.rfind(':');
            if (colon == std::string::npos) {
                redis.host = spec;
            } else {
                redis.host = spec.substr(0, colon);
                redis.port = static_cast<unsigned short>(
                    std::strtoul(spec.c_str() + colon + 1, nullptr, 10));
            }
        }
        telemetry::start(telemetry::makeRedisSink(redis));
        // Everything already written through eng::log shows up in the browser
        // without a single call site changing.
        telemetry::mirrorEngineLog("log");
        ENG_TELEMETRY("engine", telemetry::Level::Info,
                      "connector attached: %s", redis.host.c_str());
    }

    detail::registerRoot(mRenderer);
    // There is no unprofiled path: the default look ("dungeon") is a profile
    // like any other, so something is always applied here. The game's per-level
    // render palette still runs later and overrides the art-direction fields it
    // owns -- the profile sets the pipeline and the baseline look under it.
    // Command line first (it is the explicit, per-run choice), then the
    // environment, then the engine default.
    int presetId = kDefaultRenderPreset;
    if (const char* presetName = std::getenv("RAVEN_RENDER_PRESET")) {
        const int id = renderPresetFromName(presetName);
        if (id > 0)
            presetId = id;
        else
            log::warn("Unknown RAVEN_RENDER_PRESET '%s'; using the default "
                      "profile instead", presetName);
    }
    if (renderPreset > 0)
        presetId = renderPreset;
    mRenderPreset = presetId;
    applyRenderPreset(mRenderer, renderPresetValues(presetId));

    { // Step clock (stop-motion timing) from the [animation] config table.
        StepRates sr = mStepClock.rates();
        sr.enabled = mConfig.getBool("animation.enabled", sr.enabled);
        sr.scale = float(mConfig.getNumber("animation.scale", sr.scale));
        sr.phaseJitter =
            float(mConfig.getNumber("animation.phase_jitter", sr.phaseJitter));
        // render.anim_fps predates the per-channel table and used to drive every
        // stepped system at one rate. Honour it as the base for the stylised
        // channels so existing configs keep their tuning; projectiles keep their
        // own faster default because a single rate never suited them.
        const float legacy =
            float(mConfig.getNumber("render.anim_fps",
                                    sr.rate[int(StepChannel::Characters)]));
        for (StepChannel c : {StepChannel::Characters, StepChannel::Viewmodel,
                              StepChannel::World, StepChannel::Particles})
            sr.rate[int(c)] = legacy;
        for (int i = 0; i < kStepChannelCount; ++i) {
            const std::string key = std::string("animation.") +
                                    stepChannelName(StepChannel(i)) + "_fps";
            sr.rate[i] = float(mConfig.getNumber(key, sr.rate[i]));
        }
        mStepClock.setRates(sr);
    }
    // Safe before any attachMesh: entities created later join the debug
    // view through the attachMesh wireframe hook.
    if (std::getenv("RAVEN_WIREFRAME"))
        mRenderer.setWireframeDebug(true);
    if (!mInput.loadBindings(mConfig)) {
        shutdown();
        return false;
    }
    // The config and the profile the run is actually on. Both are overridable
    // three ways (file, environment, command line), so "which one won" is a
    // real question, and answering it by reading code is how a session gets
    // spent debugging a look that was never applied.
    log::info("Engine: mount set '%s', config %s, %zu actions bound",
              mountSet.c_str(), configFile.c_str(), mConfig.bindings().size());
    log::info("Engine: render profile '%s'", renderPresetName(presetId));

    const char* shot = std::getenv("RAVEN_SCREENSHOT");
    if (shot)
        mImpl->screenshotPath = shot;
    // Screenshot capture drives a fixed timestep so the frame is reproducible
    // (default 1/60 s). RAVEN_FIXED_DT overrides it (e.g. to land on a specific
    // animation phase); set it explicitly to force deterministic timing without
    // capturing.
    if (shot)
        mImpl->fixedTickDt = 1.0f / 60.0f;
    if (const char* fdt = std::getenv("RAVEN_FIXED_DT")) {
        const float v = float(std::atof(fdt));
        if (v > 0.0f)
            mImpl->fixedTickDt = v;
    }
    if (const char* frame = std::getenv("RAVEN_SCREENSHOT_FRAME"))
        mImpl->screenshotFrame = std::max(1, std::atoi(frame));
    if (const char* frames = std::getenv("RAVEN_BENCH_FRAMES")) {
        mImpl->benchmarkFrames = std::max(1, std::atoi(frames));
        mImpl->frameSamples.reserve(size_t(mImpl->benchmarkFrames));
    }
    mImpl->frameCapture = FrameCapture::fromEnvironment();
    if (mImpl->frameCapture.requested())
        log::info("RenderDoc: capture requested for frame %d",
                  mImpl->frameCapture.requestedFrame());
    // Deterministic capture: Ogre's ParticleFX emitters draw from C rand(),
    // which some init path reseeds from wall-clock time. Pin it to a constant
    // so fire/ash/spark emission is identical every run (seed here, before any
    // particle spawns in the game's level build).
    if (mImpl->fixedTickDt > 0.0f)
        std::srand(1234u);

    mImpl->hasPrev = false;
    return true;
}

void Engine::setRenderPreset(int id)
{
    mRenderPreset = id;
    applyRenderPreset(mRenderer, id);
    // A profile switch changes the whole image, so it belongs in the log the
    // screenshot is read against. Silent, it was impossible to tell "the look
    // regressed" from "something switched the profile".
    log::info("Engine: render profile -> '%s'", renderPresetName(id));
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
    const float dt = measureFrameDelta();
    // One advance per frame, on every path, so quantised time cannot desync from
    // real time (a missed advance would strand every stepped channel).
    mStepClock.advance(dt);
    // Both abstract timelines advance from the same measured delta; the game
    // clock is the one that scale/pause act on, so it is what simulation reads.
    mRealClock.update(dt);
    mGameClock.update(dt);
    return dt;
}

float Engine::measureFrameDelta()
{
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
    // the benchmark/screenshot hooks below. animDt < 0 means "take it from the
    // step clock", so particle VFX step with the rest of the scene by default --
    // smooth particles beside stepped characters are the most common way the
    // stop-motion illusion breaks. delta() returns dt unchanged when the
    // Particles channel is continuous or the clock is disabled.
    const float adt = (animDt < 0.0f)
                          ? mStepClock.delta(StepChannel::Particles)
                          : animDt;
    mRenderer.updateParticles(adt); // recycle finished one-shot particle systems
    // Loading frames are presentation only: paint and get out before any of the
    // capture/bench hooks can see them. RAVEN_CAPTURE_LOADING lifts that so the
    // loading screen itself can be screenshotted -- it is the only way to see
    // it in a deterministic capture, since by design it leaves no frames behind.
    static const bool captureLoading = std::getenv("RAVEN_CAPTURE_LOADING");
    if (mImpl->loading && !captureLoading) {
        detail::coreOf(mRenderer).renderFrame(adt);
        return;
    }
    const int renderedFrame = mImpl->frameCount + 1;
    mImpl->frameCapture.beforeFrame(renderedFrame);
    detail::coreOf(mRenderer).renderFrame(adt);
    mImpl->frameCapture.afterFrame(renderedFrame);
    if (mImpl->frameCapture.failed()) {
        log::error("RenderDoc: requested frame %d was not captured",
                   mImpl->frameCapture.requestedFrame());
        mClose = true;
    } else if (mImpl->frameCapture.completed()) {
        log::info("RenderDoc: captured frame %d",
                  mImpl->frameCapture.requestedFrame());
        mClose = true;
    }
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
    if (mImpl->recorder.active()) {
        mImpl->recorder.afterFrame(mImpl->frameCount);
        if (mImpl->recorder.complete()) {
            mImpl->recorder.encode();
            mClose = true;
        }
    }
}

void Engine::setLoadingPhase(bool loading)
{
    mImpl->loading = loading;
    // The first gameplay frame must not inherit the wall-clock gap the load
    // spent, or physics would eat a spike-clamped 100 ms step on frame one.
    if (!loading) {
        mImpl->hasPrev = false;
        // Nor the *stepped* time it spent. The load loop runs a variable number
        // of frames (it pumps work against a millisecond budget, so a slower
        // shader compile means more frames) and every one of them advanced the
        // step clock. Frame counting for captures already restarts here; the
        // animation phase every stepped system reads did not, so a capture
        // pinned to frame 300 still landed on a different viewmodel pose run to
        // run. Same boundary, same rebase.
        mStepClock.rewind();
        // Same argument for the abstract timelines: gameplay starts at t=0
        // regardless of how many frames the load took, so anything keyed off
        // absolute game time is reproducible across runs.
        mGameClock.reset();
        mRealClock.reset();
    }
}

bool Engine::loadingPhase() const { return mImpl->loading; }

void Engine::startRecording(const RecordingOptions& options)
{
    GifRecorder::Hooks hooks;
    hooks.writeFrame = [this](const std::string& path) {
        detail::coreOf(mRenderer).writeScreenshot(path);
    };
    hooks.run = [](const std::string& command) {
        return std::system(command.c_str());
    };
    mImpl->recorder = GifRecorder(options, std::move(hooks));
    if (!mImpl->recorder.active()) {
        log::error("Engine: recording requested with no frames to capture");
        return;
    }
    // Same determinism contract as the screenshot hook, and it doubles as the
    // clip's timebase: one rendered frame is one GIF frame at 1/fps.
    mImpl->fixedTickDt = 1.0f / float(options.fps);
    mImpl->hasPrev = false;
    std::srand(1234u);
    log::info("Engine: recording %d frames at %d fps from frame %d to %s",
              options.frames, options.fps, options.startFrame,
              options.path.c_str());
}

bool Engine::recording() const
{
    return mImpl->recorder.active() && !mImpl->recorder.complete();
}

void Engine::shutdown()
{
    for (auto it = mSystems.rbegin(); it != mSystems.rend(); ++it)
        (*it)->terminate();
    mSystems.clear();
    // Particle batches and decals are custom renderables holding scene nodes,
    // so they have to go before the scene manager underneath them does.
    mRenderer.shutdownParticles();
    detail::coreOf(mRenderer).shutdown(); // Ogre first
    mImpl->platform.shutdown();           // native window after
}

} // namespace eng
