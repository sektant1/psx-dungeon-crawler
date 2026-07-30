#include <eng/app/FpsGameApp.h>

#include <eng/Input.h>
#include <eng/Renderer.h>
#include <eng/controllers/FpsController.h>

namespace eng {

struct FpsGameApp::Impl
{
    FpsGameConfig cfg;
    Physics physics;
    DebugTools console;
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
FrameStats& FpsGameApp::stats() { return *mImpl->stats; }
ColliderDebug& FpsGameApp::colliderView() { return mImpl->colliders; }
const FpsGameConfig& FpsGameApp::config() const { return mImpl->cfg; }
bool FpsGameApp::uiOpen() const { return mImpl->console.visible(); }

glm::vec3 FpsGameApp::viewerPosition() const
{
    // const_cast: playerController() is the non-const accessor the console
    // needs; asking for the eye position through it does not mutate anything.
    if (FpsController* fps = const_cast<FpsGameApp*>(this)->playerController())
        return fps->eyePosition();
    return glm::vec3(0.0f);
}

bool FpsGameApp::onStart(Engine& engine)
{
    mImpl->cfg = setup(engine);
    std::vector<std::string> phases = mImpl->cfg.phases;
    mImpl->renderPhase = int(phases.size());
    phases.emplace_back("Render");
    mImpl->stats = std::make_unique<FrameStats>(std::move(phases));

    Renderer& r = engine.renderer();
    r.setCameraFov(mImpl->cfg.cameraFov);
    r.setCameraClip(mImpl->cfg.nearClip, mImpl->cfg.farClip);

    mImpl->physics.init(mImpl->cfg.physics);
    mImpl->perf.setVisible(false); // diagnostic only; the perf key reveals it

    return onStartGame(engine);
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
    if (!in.mouseGrabbed() && !mImpl->console.visible() && in.wasMouseClicked())
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
