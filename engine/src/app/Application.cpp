#include <eng/app/Application.h>

#include <eng/CodeSize.h>
#include <eng/Log.h>
#include <eng/MemoryProfiler.h>
#include <eng/telemetry/Telemetry.h>
#include <eng/ui/LoadingScreen.h>

#include <imgui.h>

#include <cstdio>
#include <cstdlib>

namespace eng {
namespace {

// Frames between treemap refreshes. The per-frame *graph* still gets every
// frame (three samples); it is the tile breakdowns -- dozens of records each --
// that drop to 10 Hz.
constexpr int kTreemapEvery = 6;
// Symbolising a call stack is expensive, so this is slower -- but it must stay
// well inside the browser's five-second staleness window, or tiles expire
// between refreshes and the pane reads as empty.
constexpr std::uint64_t kSitesEvery = 60;   // ~1 s at 60 fps
// The symbol table cannot change while the process runs; this is resent only so
// a browser opened later still finds the values in scrollback.
constexpr std::uint64_t kCodeSizeEvery = 600; // ~10 s

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

    // RAVEN_LOADING_HOLD keeps the screen up for at least N seconds after the
    // work is done. Purely a development aid: a plan that finishes in 200 ms is
    // impossible to look at, let alone tune the animation against.
    float hold = 0.0f;
    if (const char* h = std::getenv("RAVEN_LOADING_HOLD"))
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

    // Parse the symbol table here, while the loading screen is still up: it is
    // a ~290 ms one-off, and the alternative is paying it on whichever frame
    // first asks for it, which is a visible hitch in a running game.
    {
        ENG_MEM_TAG("codesize");
        codesize::warm();
    }

    engine.setLoadingPhase(false);
    for (const LoadTiming& t : runner.timings())
        log::info("Load: %-28s %7.1f ms", t.label.c_str(), double(t.ms));
    log::info("Load: %d steps in %.0f ms", runner.count(),
              double(runner.elapsedMs()));
    return runner.done();
}

// Static code size, once. The symbol table does not change while the process
// runs, so this is a startup cost and never a per-frame one.
//
// It rides the same channel and the same `<group>.<name>` convention as the
// heap, because it is the same view: a treemap sized by bytes. The only thing
// that differs is which bytes.
void publishCodeSize()
{
    if (!telemetry::enabled("mem", telemetry::Level::Trace))
        return;
    ENG_MEM_TAG("telemetry");
    // Resent on a slow timer rather than only at startup. A watch is a current
    // value and the collector keeps a bounded scrollback, so a value published
    // once and never again is a value that has scrolled out of existence by the
    // time anybody opens the browser. Sixty records every few seconds is
    // nothing, and it means the view is populated whenever you arrive.
    //
    // Cheap only because byOwner() is cached and warmed during load. Before it
    // was, this re-demangled 108k symbols on the timer and cost ~290 ms every
    // time -- a periodic freeze, caused entirely by the tool watching for them.
    if (!codesize::available()) {
        telemetry::watch("mem", "size.unavailable",
                         "no symbol table (stripped, or not ELF)");
        return;
    }
    char name[128];
    int published = 0;
    const std::vector<codesize::Symbol>& owners = codesize::byOwner();
    const std::uint64_t total = codesize::codeBytes();
    for (const codesize::Symbol& owner : owners) {
        // The tail of a code-size list is thousands of entries of a few hundred
        // bytes each. They are real, they are not the answer, and drawing them
        // turns the map into noise at the bottom right.
        if (owner.bytes < 4096 || ++published > 60)
            break;
        std::snprintf(name, sizeof(name), "size.%s", owner.name.c_str());
        telemetry::watchValue("mem", name, double(owner.bytes));
    }
    // Logged from the pass already made, not from two more of them: totalBytes()
    // and functions() would each re-read and re-demangle the whole symbol table.
    static bool logged = false;
    if (!logged) {
        logged = true;
        log::info("codesize: %.2f MB of function code in %zu modules",
                  double(total) / (1024.0 * 1024.0), owners.size());
    }
}

// CPU time per frame phase, from the profiler that is already running.
//
// Self ms, not inclusive: the treemap nests nothing, so inclusive time would
// count "render" and everything inside it twice and the areas would stop
// summing to the frame. Self time is what the phase itself cost, and those DO
// sum to the frame -- which is exactly the property a treemap needs to be
// readable as proportion.
void publishLoad(const Profiler& prof)
{
    if (!telemetry::enabled("mem", telemetry::Level::Trace))
        return;
    ENG_MEM_TAG("telemetry");
    char name[128];
    // Self ms wobbles frame to frame; the treemap is read by a human at a
    // glance. Publishing the phase breakdown at 10 Hz rather than 60 costs the
    // reader nothing and takes five sixths of the records off the pipe.
    static int tick = 0;
    if (tick++ % kTreemapEvery != 0)
        return;
    for (int i : prof.preorder()) {
        const Profiler::Node& node = prof.nodes()[std::size_t(i)];
        if (i == 0)
            continue; // the root is the whole frame, not a phase of it
        std::snprintf(name, sizeof(name), "load.%s", node.name);
        telemetry::watchValue("mem", name, prof.selfMs(i));
    }
}

// Pushes the heap numbers onto the "mem" telemetry channel, where Connector
// graphs them. This is the whole reason the memory profiler reports rather than
// draws: the browser already has the plotting, the history and the second
// monitor, and this renderer's framebuffer is a third of the window and frozen.
//
// Sample vs watch is the distinction that makes the view readable. Per-frame
// churn is a number over time and belongs on a graph; live totals and the
// per-phase table are current values that should update in place rather than
// scroll a thousand identical lines past.
void publishMemory()
{
    if (!memprof::enabled() ||
        !telemetry::enabled("mem", telemetry::Level::Trace))
        return;

    // Formatting a record allocates, and those bytes are real -- charge them to
    // a phase of their own instead of letting them land on "untagged" and look
    // like a leak in something else.
    ENG_MEM_TAG("telemetry");

    const memprof::Stats s = memprof::stats();
    const double kb = 1024.0, mb = 1024.0 * 1024.0;

    // Graphed over time: the frame's churn. A flat line near zero is a frame
    // that does not allocate, which is the thing you are trying to get to.
    telemetry::sample("mem", "alloc_kb", double(s.frameAllocBytes) / kb);
    telemetry::sample("mem", "free_kb", double(s.frameFreeBytes) / kb);
    telemetry::sample("mem", "live_mb", double(s.liveBytes) / mb);

    // Current values, updated in place.
    telemetry::watchValue("mem", "live_blocks", double(s.liveBlocks));
    telemetry::watchValue("mem", "peak_mb", double(s.peakBytes) / mb);
    telemetry::watchValue("mem", "allocs_per_frame", double(s.frameAllocs));
    telemetry::watchValue("mem", "frees_per_frame", double(s.frameFrees));
    telemetry::watchValue("mem", "header_overhead_kb",
                          double(s.overheadBytes) / kb);
    if (s.corruptions != 0)
        telemetry::watchValue("mem", "CORRUPT_HEADERS", double(s.corruptions));

    // The treemap's data, and the reason these are raw byte counts rather than
    // the pretty strings a log line would carry: the browser needs a number per
    // name to size a rectangle by, and formatting it here would mean parsing it
    // back out there. `phase.` is live bytes, `churn.` is bytes allocated last
    // frame -- two different treemaps of the same set of names.
    //
    // tagAt() rather than tags(): the vector version would allocate, once per
    // frame, in order to report allocations.
    //
    // At 10 Hz, for the same reason as publishLoad -- and it matters more here,
    // because this was the single largest tag in the first real capture. A
    // profiler that is the biggest thing in its own report is measuring itself.
    static int tick = 0;
    if (tick++ % kTreemapEvery != 0)
        return;
    char name[96];
    const int count = memprof::tagCount();
    for (int i = 0; i < count; ++i) {
        memprof::TagStat t;
        if (!memprof::tagAt(i, t) || (t.liveBytes == 0 && t.frameBytes == 0))
            continue;
        std::snprintf(name, sizeof(name), "phase.%s", t.name);
        telemetry::watchValue("mem", name, double(t.liveBytes));
        std::snprintf(name, sizeof(name), "churn.%s", t.name);
        telemetry::watchValue("mem", name, double(t.frameBytes));
    }

}

// The sampled call stacks, which are the expensive half: resolving a symbol
// costs a backtrace_symbols and a demangle per frame per site.
//
// Its own function with its own cadence, deliberately. Nesting this inside
// publishMemory put it behind two gates -- that function's 10 Hz throttle and a
// 60-frame counter of its own -- which multiplied out to one refresh every six
// seconds, against a five-second staleness window in the browser. Every tile
// expired before its replacement arrived and the pane was permanently empty.
// One cadence, stated once, checked in one place.
void publishSites()
{
    if (!memprof::enabled() ||
        !telemetry::enabled("mem", telemetry::Level::Trace))
        return;
    ENG_MEM_TAG("telemetry");
    const memprof::Stats s = memprof::stats();
    if (s.sampleRate != 0) {
        char name[128];
        // New stacks resolved per call. Names are memoised for the life of the
        // process, so this is only ever paid for stacks seen for the first
        // time; the cap keeps a burst of them from landing on one frame. Four
        // per second finds a new site quickly and costs nothing steady-state.
        int budget = 4;
        for (const memprof::StackStat& site : memprof::stacks(16)) {
            if (site.liveBytes == 0)
                continue;
            // Already scaled by stacks(), which is the only place that knows
            // which of these bytes were sampled and which were counted whole.
            // Scaling again here is what made this claim 1.6 GB live against an
            // exact 38 MB.
            // siteName decrements `budget` itself, and only for a name it
            // actually had to resolve. Empty means it declined -- that site
            // appears on a later pass.
            const std::string label = memprof::siteName(site, &budget);
            if (label.empty())
                continue;
            std::snprintf(name, sizeof(name), "site.%s", label.c_str());
            telemetry::watchValue("mem", name, double(site.liveBytes));
        }
    }
}

} // namespace

int runApplication(Application& app, int argc, char** argv)
{
    const AppConfig cfg = app.configure(argc, argv);

    Engine engine;
    if (!engine.init(cfg.configPath, cfg.mountSet, cfg.renderPreset,
                     cfg.projectDir))
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
    publishCodeSize();

    Profiler& prof = engine.profiler();
    // RAVEN_PROFILE=N dumps the frame's timing hierarchy every N frames (any
    // non-numeric value means 120). The console's `profile` command is the same
    // data on demand; this is the version that works over ssh, in a capture run,
    // or when the thing being profiled is the UI.
    int profileEvery = 0;
    if (const char* pp = std::getenv("RAVEN_PROFILE")) {
        profileEvery = std::atoi(pp);
        if (profileEvery <= 0)
            profileEvery = 120;
    }

    while (!engine.shouldClose()) {
        const float realDt = engine.tick();
        // Simulation runs on the game timeline, not the wall clock: pause and
        // slow-motion are then a property of the clock rather than something
        // every system has to check. Presentation reads the same delta, so a
        // paused world is genuinely still; anything that must keep moving over
        // it (debug camera, UI) reads f.realDt.
        const float dt = engine.gameClock().delta();

        prof.beginFrame();
        // Zeroed here, published in endFrame, so the churn numbers below cover
        // exactly one frame and a reader never catches a half-counted one.
        memprof::beginFrame();

        // alpha is only known after the fixed loop has drained, so the context
        // is rebuilt for the phases that can actually use it.
        FrameContext f{engine, dt, 1.0f, frame, realDt};
        // Stamped before anything writes, so every record this frame carries
        // the frame that produced it -- which is the correlation that makes a
        // channel worth more than a log file.
        telemetry::setFrame(frame);
        if (telemetry::enabled("frame", telemetry::Level::Trace)) {
            // Watched rather than logged: these are the numbers you glance at,
            // and sixty scrolling lines a second of "frame_ms = 16.7" is how a
            // log becomes something you filter out rather than read.
            telemetry::watchValue("frame", "frame_ms", realDt * 1000.0f);
            telemetry::watchValue("frame", "fps",
                                  realDt > 0.0f ? 1.0f / realDt : 0.0f);
            telemetry::watchValue("frame", "game_ms", dt * 1000.0f);
            std::size_t batches = 0, triangles = 0;
            engine.renderer().frameStats(batches, triangles);
            telemetry::watchValue("render", "batches", double(batches));
            telemetry::watchValue("render", "triangles", double(triangles));
        }
        {
            ENG_PROFILE(prof, "frame begin");
            app.onFrameBegin(f);
        }

        if (fixed) {
            ENG_PROFILE(prof, "fixed step");
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

        {
            ENG_PROFILE(prof, "update");
            app.onUpdate(f);
        }

        if (cfg.imgui) {
            ENG_PROFILE(prof, "gui");
            // Real delta: imgui animates its own widgets, and a paused game
            // should not freeze the console you paused it from.
            engine.beginImGuiFrame(realDt);
            app.onGui(f);
        }

        const auto renderStart = std::chrono::steady_clock::now();
        {
            ENG_PROFILE(prof, "render");
            engine.renderFrame(realDt);
        }
        app.onFrameRendered(std::chrono::duration<float, std::milli>(
                                std::chrono::steady_clock::now() - renderStart)
                                .count());
        prof.endFrame();
        memprof::endFrame();
        prof.logTreeEvery(profileEvery);
        publishMemory();
        publishLoad(prof);
        // Every cadence in one place, in frames, so they can be read against
        // each other and against the browser's staleness window.
        if (frame % kSitesEvery == 0)
            publishSites();
        if (frame % kCodeSizeEvery == kCodeSizeEvery - 1)
            publishCodeSize();
        ++frame;
    }

    app.onShutdown(engine);
    engine.shutdown();
    return app.exitCode;
}

} // namespace eng
