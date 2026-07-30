#pragma once
#include <chrono>
#include <string>
#include <vector>

namespace eng {

// A read-only window onto whatever the application measures per frame. The
// engine plots and labels these without knowing what a phase *is* -- one game's
// "Weapons" is another's "AI" -- so the HUD stays engine tooling while the
// taxonomy stays with the application. Pointers must outlive the draw call.
struct FrameStatsView {
    const float* frameHist = nullptr; // ring of frame times, milliseconds
    int histCount = 0;
    int histHead = 0;                 // next write index into frameHist
    const char* const* phaseNames = nullptr;
    const float* phaseMs = nullptr;
    int phaseCount = 0;
};

// Per-frame CPU timing: a ring of frame times plus one accumulator per named
// phase, in the shape the debug HUD already reads (FrameStatsView).
//
// The engine measures and plots; the *taxonomy* stays with the application --
// one game's "Weapons" is another's "AI" -- so the phase names are constructor
// arguments, not an enum in here. Every game was otherwise writing this same
// ring buffer plus a std::chrono stopwatch by hand.
//
// Usage per frame:
//   stats.beginFrame();
//   { auto t = stats.time(kPhysics); ...work... }   // RAII, adds to that phase
//   stats.endFrame(dt * 1000.0f);
class FrameStats
{
public:
    static constexpr int kHistory = 120; // ~2 s at 60 fps, the HUD's plot width

    // `phaseNames` becomes the phase order: index i in time()/setPhase() is
    // phaseNames[i]. Copied, so temporaries are fine.
    explicit FrameStats(std::vector<std::string> phaseNames);

    // Zeroes the phase accumulators. Call once at the top of the frame; without
    // it, a phase that is skipped this frame keeps last frame's number.
    void beginFrame();
    // Records the whole-frame time into the ring.
    void endFrame(float totalMs);

    // Scoped stopwatch: adds its lifetime to `phase`. Nested/repeated scopes on
    // the same phase accumulate, so a phase split across the loop still reads
    // as one number.
    class Scope
    {
    public:
        Scope(FrameStats& stats, int phase)
            : mStats(stats), mPhase(phase),
              mStart(std::chrono::steady_clock::now())
        {
        }
        ~Scope()
        {
            mStats.addPhase(mPhase, std::chrono::duration<float, std::milli>(
                                        std::chrono::steady_clock::now() - mStart)
                                        .count());
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        FrameStats& mStats;
        int mPhase;
        std::chrono::steady_clock::time_point mStart;
    };

    [[nodiscard]] Scope time(int phase) { return Scope(*this, phase); }

    // For work that is already timed elsewhere (a fixed-step loop summing its
    // own steps, say). Out-of-range indices are ignored.
    void addPhase(int phase, float ms);
    void setPhase(int phase, float ms);
    float phaseMs(int phase) const;

    // What the console and the perf HUD read. Valid while this object lives.
    FrameStatsView view() const;

    // One-line breakdown to the log, for profiling with no UI up. `everyN`
    // makes the common "log every 120 frames" case a call, not a static counter
    // at each site: returns without logging until the counter comes round.
    void logSummary() const;
    void logSummaryEvery(int everyN);

private:
    std::vector<std::string> mNames;
    std::vector<const char*> mNamePtrs; // stable char* for FrameStatsView
    std::vector<float> mPhaseMs;
    float mFrameHist[kHistory] = {0.0f};
    int mHistHead = 0;
    int mLogTick = 0;
};

} // namespace eng
