#pragma once

#include <functional>
#include <string>
#include <vector>

namespace eng {

// Staged, time-sliced startup work.
//
// Everything an app does before its first playable frame -- cooking a scene,
// parsing TOML libraries, building physics bodies, compiling shaders -- used to
// sit in one blocking onStart(), which meant the window stayed frozen and black
// for as long as it took and nobody could see which part was slow. A LoadPlan
// turns that into an ordered list of named, weighted steps the frame loop pumps
// a few milliseconds at a time, so the loading screen keeps animating and the
// cost of each stage is measurable.
//
// This layer knows nothing about the renderer on purpose: it is plain data plus
// callbacks, so it is unit-testable without a window (see LoadingTests).
//
// Steps run on the main thread. Deliberate: the renderer and Ogre's resource
// manager are not thread-safe here, and a step that uploads a mesh from a
// worker would be a crash, not a speedup. A step that wants to be interruptible
// splits itself with tick() instead.
struct LoadStep {
    std::string label;   // shown on the loading screen
    float weight = 1.0f; // relative cost; drives the progress bar
    // Returns true when finished. A step that returns false is called again on
    // the next slice, which is how a long loop (100 rooms, 4000 props) reports
    // progress instead of blocking the frame.
    std::function<bool()> tick;
};

// An ordered list of steps. Built once, before the loading loop starts.
class LoadPlan {
public:
    // One-shot step: runs to completion inside a single slice.
    void add(std::string label, std::function<void()> work, float weight = 1.0f);
    // Resumable step: `work` returns true when done.
    void addResumable(std::string label, std::function<bool()> work,
                      float weight = 1.0f);

    const std::vector<LoadStep>& steps() const { return mSteps; }
    bool empty() const { return mSteps.empty(); }
    float totalWeight() const;

private:
    std::vector<LoadStep> mSteps;
};

// Timing for one finished step, for the "what was slow" report.
struct LoadTiming {
    std::string label;
    float ms = 0.0f;
};

// Drives a LoadPlan a slice at a time. The frame loop calls slice(budgetMs)
// once per frame and draws LoadProgress until done().
class LoadRunner {
public:
    // Clock injection: tests drive a fake clock, so slicing behaviour is
    // verifiable without sleeping. Returns milliseconds, monotonic.
    using Clock = std::function<double()>;

    explicit LoadRunner(LoadPlan plan, Clock clock = {});

    // Runs steps until the budget is spent or the plan finishes. A single step
    // always gets at least one tick(), so a step slower than the budget still
    // makes progress instead of deadlocking the loop.
    void slice(float budgetMs);

    bool done() const { return mIndex >= mPlan.steps().size(); }
    // 0..1 by accumulated weight. A resumable step counts as finished weight
    // only once it is done, so the bar never runs backwards.
    float progress() const;
    // Label of the step being worked on, or the last one when done.
    const std::string& label() const { return mLabel; }
    // Steps finished so far / total.
    int completed() const { return int(mIndex); }
    int count() const { return int(mPlan.steps().size()); }

    const std::vector<LoadTiming>& timings() const { return mTimings; }
    // Total wall time across all slices, in milliseconds.
    float elapsedMs() const { return mElapsedMs; }

    // Runs the whole plan with no budget. For headless tools and tests.
    void runAll();

private:
    LoadPlan mPlan;
    Clock mClock;
    size_t mIndex = 0;
    float mDoneWeight = 0.0f;
    float mTotalWeight = 0.0f;
    float mStepStartMs = 0.0f;
    float mElapsedMs = 0.0f;
    bool mStepStarted = false;
    std::string mLabel;
    std::vector<LoadTiming> mTimings;
};

} // namespace eng
