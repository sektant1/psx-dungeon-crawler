#include <eng/app/Application.h>

namespace eng {

int runApplication(Application& app, int argc, char** argv)
{
    const AppConfig cfg = app.configure(argc, argv);

    Engine engine;
    if (!engine.init(cfg.configPath, cfg.assetDir, cfg.renderPreset))
        return app.exitCode ? app.exitCode : 1;

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
