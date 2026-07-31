#include <eng/app/FpsGameApp.h>

#include <eng/Input.h>
#include <eng/render/Warmup.h>
#include <eng/Log.h>

#include <cstdio>
#include <cstdlib>
#include <eng/Renderer.h>
#include <eng/controllers/FpsController.h>

namespace eng {

struct FpsGameApp::Impl
{
    FpsGameConfig cfg;
    Physics physics;
    DebugTools console;
    DebugConsole dev;
    PerfOverlay perf;
    ColliderDebug colliders;
    std::unique_ptr<FrameStats> stats;
    int renderPhase = 0;
    float lastRenderMs = 0.0f;
};

FpsGameApp::FpsGameApp() : mImpl(std::make_unique<Impl>()) {}
FpsGameApp::~FpsGameApp() = default;

Physics& FpsGameApp::physics() { return mImpl->physics; }
DebugTools& FpsGameApp::console() { return mImpl->console; }
DebugConsole& FpsGameApp::devConsole() { return mImpl->dev; }
FrameStats& FpsGameApp::stats() { return *mImpl->stats; }
ColliderDebug& FpsGameApp::colliderView() { return mImpl->colliders; }
const FpsGameConfig& FpsGameApp::config() const { return mImpl->cfg; }
bool FpsGameApp::uiOpen() const
{
    return mImpl->console.visible() || mImpl->dev.visible();
}

glm::vec3 FpsGameApp::viewerPosition() const
{
    // const_cast: playerController() is the non-const accessor the console
    // needs; asking for the eye position through it does not mutate anything.
    if (FpsController* fps = const_cast<FpsGameApp*>(this)->playerController())
        return fps->eyePosition();
    return glm::vec3(0.0f);
}

void FpsGameApp::onLoad(Engine& engine, LoadPlan& plan)
{
    // Three ordered stages, all under the loading screen: the engine's own
    // setup, then whatever the game wants to stream in, then the world build,
    // then renderer warmup last (it can only compile what the world referenced).
    // setup() runs here rather than inside a step: it is pure configuration,
    // and the steps below have to know what it decided (warmup, phases).
    mImpl->cfg = setup(engine);
    plan.add("Preparing systems", [this, &engine] { startSystems(engine); },
             1.0f);
    onLoadGame(engine, plan);
    plan.add("Building the world",
             [this, &engine] {
                 if (!onStartGame(engine)) {
                     exitCode = exitCode ? exitCode : 1;
                     engine.requestClose();
                 }
             },
             8.0f);
    if (mImpl->cfg.warmupRenderer)
        addRenderWarmup(plan);
}

void FpsGameApp::startSystems(Engine& engine)
{
    std::vector<std::string> phases = mImpl->cfg.phases;
    mImpl->renderPhase = int(phases.size());
    phases.emplace_back("Render");
    mImpl->stats = std::make_unique<FrameStats>(std::move(phases));

    Renderer& r = engine.renderer();
    r.setCameraFov(mImpl->cfg.cameraFov);
    r.setCameraClip(mImpl->cfg.nearClip, mImpl->cfg.farClip);

    mImpl->physics.init(mImpl->cfg.physics);
    mImpl->perf.setVisible(false); // diagnostic only; the perf key reveals it

    // Engine-level commands every game on this base gets for free. Anything
    // game-specific is registered by the game from onStartGame.
    DebugConsole& dev = mImpl->dev;
    dev.captureEngineLog();
    dev.registerCommand("quit", "close the application",
                        [&engine](const DebugConsole::Args&) {
                            engine.requestClose();
                        });
    dev.registerCommand(
        "r.preset", "list render profiles, or switch to one by id",
        [this, &engine](const DebugConsole::Args& a) {
            const auto presets = renderPresets();
            if (a.size() > 1) {
                const int id = std::atoi(a[1].c_str());
                engine.setRenderPreset(id);
                mImpl->dev.print(log::Level::Info, "render",
                                 "render preset -> " + a[1]);
                return;
            }
            for (const auto& p : presets)
                mImpl->dev.print(log::Level::Info, "render",
                                 std::to_string(p.id) + "  " + p.name);
        });
    dev.registerCommand("stats", "print the current frame breakdown",
                        [this](const DebugConsole::Args&) {
                            const FrameStatsView v = mImpl->stats->view();
                            // The ring's newest sample is the one before the
                            // write head; endFrame has already run this frame.
                            float ms = 0.0f;
                            if (v.frameHist && v.histCount > 0)
                                ms = v.frameHist[(v.histHead + v.histCount - 1) %
                                                 v.histCount];
                            char buf[256];
                            std::snprintf(buf, sizeof(buf), "%.2f ms  (%.0f fps)",
                                          double(ms),
                                          ms > 0.0f ? 1000.0 / double(ms) : 0.0);
                            mImpl->dev.print(log::Level::Info, "stats", buf);
                            for (int i = 0; i < v.phaseCount; ++i) {
                                std::snprintf(buf, sizeof(buf), "  %-12s %6.2f ms",
                                              v.phaseNames[i],
                                              double(v.phaseMs[i]));
                                mImpl->dev.print(log::Level::Info, "stats", buf);
                            }
                        });
    dev.registerCommand("colliders", "toggle the collider overlay",
                        [this](const DebugConsole::Args&) {
                            mImpl->colliders.enabled = !mImpl->colliders.enabled;
                        });
    dev.registerCommand("perf", "toggle the performance HUD",
                        [this](const DebugConsole::Args&) {
                            mImpl->perf.toggle();
                        });
    dev.registerCommand("tune", "toggle the tuning panel (tabs/sliders)",
                        [this](const DebugConsole::Args&) {
                            mImpl->console.toggle();
                        });
}

bool FpsGameApp::onStart(Engine& engine)
{
    // Everything moved into the load plan; what is left is the seam the base
    // class still owes Application.
    (void)engine;
    return exitCode == 0;
}

void FpsGameApp::onFrameBegin(const FrameContext& f)
{
    Input& in = f.engine.input();
    const FpsGameConfig& cfg = mImpl->cfg;
    mImpl->stats->beginFrame();
    // Carried over from the previous frame: the render happens after that
    // frame's stats are closed, and beginFrame() would otherwise wipe it.
    mImpl->stats->setPhase(mImpl->renderPhase, mImpl->lastRenderMs);

    // First press releases the mouse, second quits; a click grabs it back.
    // Suppressed while the console is open, where the cursor belongs to the UI
    // and the click is the panel's.
    if (!cfg.quitAction.empty() && in.wasPressed(cfg.quitAction.c_str())) {
        if (in.mouseGrabbed())
            in.setMouseGrab(false);
        else
            f.engine.requestClose();
    }
    if (!cfg.consoleAction.empty() && in.wasPressed(cfg.consoleAction.c_str())) {
        mImpl->console.toggle();
        if (mImpl->console.visible())
            in.setMouseGrab(false);
    }
    if (!cfg.devConsoleAction.empty() &&
        in.wasPressed(cfg.devConsoleAction.c_str())) {
        mImpl->dev.toggle();
        if (mImpl->dev.visible())
            in.setMouseGrab(false);
    }
    if (!in.mouseGrabbed() && !uiOpen() && in.wasMouseClicked())
        in.setMouseGrab(true);

    if (!cfg.colliderAction.empty() && in.wasPressed(cfg.colliderAction.c_str()))
        mImpl->colliders.enabled = !mImpl->colliders.enabled;
    if (!cfg.perfAction.empty() && in.wasPressed(cfg.perfAction.c_str()))
        mImpl->perf.toggle();

    onInput(f);
}

void FpsGameApp::onFixedStep(const FrameContext& f, float fixedDt)
{
    // Phase 0 is the simulation phase by construction: a game that names its
    // phases lists that one first. Nothing breaks if it has none.
    const auto timed = mImpl->stats->time(0);
    onPreSimulate(f, fixedDt);
    mImpl->physics.update(fixedDt);
    onPostSimulate(f, fixedDt);
}

void FpsGameApp::onUpdate(const FrameContext& f)
{
    // Every render-side read of a body transform this frame interpolates by
    // this much. Set before anything presents, once, by the engine -- a game
    // forgetting it is a whole class of judder bugs.
    mImpl->physics.setInterpolationAlpha(f.alpha);
    onPresent(f);
    mImpl->stats->endFrame(f.dt * 1000.0f);
}

void FpsGameApp::onGui(const FrameContext& f)
{
    Renderer& r = f.engine.renderer();
    const FrameStatsView view = mImpl->stats->view();

    DebugTools::Deps deps;
    deps.renderer = &r;
    deps.fps = playerController();
    deps.frame = &view;
    deps.colliders = &mImpl->colliders;
    deps.steps = &f.engine.stepClock();
    deps.renderPresetId = f.engine.renderPreset();
    mImpl->console.draw(deps);
    mImpl->dev.draw();
    mImpl->perf.draw(&view, &r);

    ColliderOverlayOptions overlay;
    overlay.staticLayers = mImpl->cfg.staticLayers;
    overlay.viewer = viewerPosition();
    overlay.sweepDt = 1.0f / 60.0f;
    drawColliderOverlay(mImpl->physics, r, mImpl->colliders, overlay);

    onGameGui(f);
}

void FpsGameApp::onFrameRendered(float renderMs)
{
    // Lands one frame late by construction: the render happens after the stats
    // for this frame are closed. At 60 fps that is invisible on a plot, and the
    // alternative is not measuring the phase at all.
    mImpl->lastRenderMs = renderMs;
    mImpl->stats->setPhase(mImpl->renderPhase, renderMs);
}

void FpsGameApp::onShutdown(Engine& engine)
{
    onStopGame(engine);
    mImpl->physics.shutdown();
}

} // namespace eng
