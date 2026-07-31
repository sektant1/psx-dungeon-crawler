#include <eng/app/Application.h>

#include <eng/Log.h>
#include <eng/ui/LoadingScreen.h>

#include <imgui.h>

#include <cstdlib>

namespace eng {
namespace {

// Pumps the app's load plan with the loading screen up. Returns false if the
// user closed the window mid-load, in which case the run ends before onStart
// (a half-built world is never handed to the game loop).
bool runLoadPlan(Application& app, Engine& engine, const AppConfig& cfg)
{
    LoadPlan plan;
    app.onLoad(engine, plan);
    if (plan.empty())
        return true;

    ui::UiCanvas canvas;
    const bool drawScreen = cfg.loadingScreen && canvas.initialise();
    LoadRunner runner(std::move(plan));
    engine.setLoadingPhase(true);

    // PSX_LOADING_HOLD keeps the screen up for at least N seconds after the
    // work is done. Purely a development aid: a plan that finishes in 200 ms is
    // impossible to look at, let alone tune the animation against.
    float hold = 0.0f;
    if (const char* h = std::getenv("PSX_LOADING_HOLD"))
        hold = float(std::atof(h));

    float elapsed = 0.0f;
    while (!engine.shouldClose() && (!runner.done() || elapsed < hold)) {
        const float dt = engine.tick();
        elapsed += dt;

        if (drawScreen) {
            engine.beginImGuiFrame(dt);
            ui::LoadingView view;
            view.progress = runner.progress();
            view.title = cfg.loadingTitle;
            view.step = runner.label();
            view.hint = cfg.loadingHint;
            view.completed = runner.completed();
            view.total = runner.count();
            view.time = elapsed;
            canvas.begin(ImGui::GetIO().DisplaySize.x > 0.0f
                             ? glm::vec2(ImGui::GetIO().DisplaySize.x,
                                         ImGui::GetIO().DisplaySize.y)
                             : glm::vec2(640.0f, 480.0f),
                         {320, 240});
            ui::drawLoadingScreen(canvas, view);
        }
        // Present first, work second: the step that runs this frame is the one
        // whose label the player just saw, and the first frame of the screen is
        // on screen before anything expensive blocks.
        engine.renderFrame(dt, 0.0f);
        runner.slice(cfg.loadBudgetMs);
    }

    engine.setLoadingPhase(false);
    for (const LoadTiming& t : runner.timings())
        log::info("Load: %-28s %7.1f ms", t.label.c_str(), double(t.ms));
    log::info("Load: %d steps in %.0f ms", runner.count(),
              double(runner.elapsedMs()));
    return runner.done();
}

} // namespace

int runApplication(Application& app, int argc, char** argv)
{
    const AppConfig cfg = app.configure(argc, argv);

    Engine engine;
    if (!engine.init(cfg.configPath, cfg.assetDir, cfg.renderPreset))
        return app.exitCode ? app.exitCode : 1;

    if (!runLoadPlan(app, engine, cfg)) {
        app.onShutdown(engine);
        engine.shutdown();
        return app.exitCode;
    }

    if (!app.onStart(engine)) {
        app.onShutdown(engine);
        engine.shutdown();
        return app.exitCode ? app.exitCode : 1;
    }

    const bool fixed = cfg.fixedDt > 0.0f;
    float accumulator = 0.0f;
    uint64_t frame = 0;

    while (!engine.shouldClose()) {
        const float dt = engine.tick();

        // alpha is only known after the fixed loop has drained, so the context
        // is rebuilt for the phases that can actually use it.
        FrameContext f{engine, dt, 1.0f, frame};
        app.onFrameBegin(f);

        if (fixed) {
            accumulator += dt;
            int steps = 0;
            while (accumulator >= cfg.fixedDt && steps++ < cfg.maxFixedSteps) {
                app.onFixedStep(f, cfg.fixedDt);
                accumulator -= cfg.fixedDt;
            }
            // Drop the backlog a slow frame left behind instead of paying it off
            // over the next frames (which is the spiral this guard exists for).
            if (steps > cfg.maxFixedSteps)
                accumulator = 0.0f;
            f.alpha = accumulator / cfg.fixedDt;
        }

        app.onUpdate(f);

        if (cfg.imgui) {
            engine.beginImGuiFrame(dt);
            app.onGui(f);
        }

        const auto renderStart = std::chrono::steady_clock::now();
        engine.renderFrame(dt);
        app.onFrameRendered(std::chrono::duration<float, std::milli>(
                                std::chrono::steady_clock::now() - renderStart)
                                .count());
        ++frame;
    }

    app.onShutdown(engine);
    engine.shutdown();
    return app.exitCode;
}

} // namespace eng
