// The cost readout in the corner of the 3D view.
//
// The number itself is trivial; what is worth pinning is that it stays
// readable. A frame counter that flickers through three digits, or a triangle
// count printed as one unbroken run of digits, is a readout an author stops
// looking at -- and a readout nobody looks at is the same as no readout, which
// is the state this replaced.

#include "ViewportOverlay.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorViewportOverlayTests: " << message << '\n';
        std::exit(1);
    }
}

static bool contains(const std::vector<StatsLine>& lines,
                     const std::string& needle)
{
    for (const StatsLine& line : lines)
        if (line.text.find(needle) != std::string::npos)
            return true;
    return false;
}

static const StatsLine* lineWith(const std::vector<StatsLine>& lines,
                                 const std::string& needle)
{
    for (const StatsLine& line : lines)
        if (line.text.find(needle) != std::string::npos)
            return &line;
    return nullptr;
}

int main()
{
    const FrameBudget budget; // the shipped defaults

    // --- what the readout says ----------------------------------------------
    {
        FrameStats stats;
        stats.fps = 60.0f;
        stats.frameMs = 16.7f;
        stats.batches = 42;
        stats.triangles = 123456;
        stats.entities = 133;

        const std::vector<StatsLine> lines = frameStatsLines(stats, budget);
        require(contains(lines, "60.0 fps"), "the frame rate is reported");
        require(contains(lines, "16.7 ms"), "and the time it took");
        require(contains(lines, "42 batches"), "the draw batches");
        require(contains(lines, "123 456 tris"),
                "and the triangles, grouped -- a six digit run is a magnitude "
                "nobody reads at a glance");
        require(contains(lines, "133 entities"), "and what the scene holds");
        require(!contains(lines, "selected"),
                "with no selection line when nothing is selected");
    }

    // --- selection is reported only when there is one -----------------------
    {
        FrameStats stats;
        stats.selected = 7;
        require(contains(frameStatsLines(stats, budget), "7 selected"),
                "a selection is counted");
    }

    // --- over budget is called out ------------------------------------------
    {
        FrameStats stats;
        stats.frameMs = budget.frameMs + 10.0f;
        stats.batches = budget.batches + 1;
        stats.triangles = budget.triangles + 1;

        const std::vector<StatsLine> lines = frameStatsLines(stats, budget);
        require(lineWith(lines, "ms")->overBudget,
                "a slow frame is flagged, because the point of the readout is "
                "to notice before the room is finished");
        require(lineWith(lines, "batches")->overBudget, "so are the batches");
        require(lineWith(lines, "tris")->overBudget, "and the triangles");
        require(!lineWith(lines, "entities")->overBudget,
                "but the entity count has no budget -- it is a fact about the "
                "document, not a cost");
    }

    // --- inside budget is quiet ---------------------------------------------
    {
        FrameStats stats;
        stats.frameMs = budget.frameMs - 1.0f;
        stats.batches = budget.batches;
        stats.triangles = budget.triangles;
        for (const StatsLine& line : frameStatsLines(stats, budget))
            require(!line.overBudget,
                    "exactly at the budget is not over it -- a readout that "
                    "cries at the target is one that always cries");
    }

    // --- smoothing: timings settle, counts do not ---------------------------
    {
        FrameStatsSmoother smoother;
        FrameStats sample;
        sample.fps = 100.0f;
        sample.frameMs = 10.0f;
        sample.batches = 50;

        smoother.update(sample, 0.1f);
        require(smoother.smoothed().frameMs > 0.0f &&
                    smoother.smoothed().frameMs < sample.frameMs,
                "one sample moves the average part of the way");
        require(smoother.smoothed().batches == 50,
                "but the batch count is taken whole: a count that lags the "
                "placement which caused it is not smoother, it is wrong");

        for (int i = 0; i < 200; ++i)
            smoother.update(sample, 0.1f);
        require(smoother.smoothed().frameMs > 9.9f &&
                    smoother.smoothed().frameMs < 10.1f,
                "and a steady frame time converges on itself");

        // A single spike must not throw the readout: that is the whole reason
        // it is smoothed.
        FrameStats spike = sample;
        spike.frameMs = 200.0f;
        smoother.update(spike, 0.1f);
        require(smoother.smoothed().frameMs < 40.0f,
                "one stalled frame nudges the average rather than replacing it");
    }

    // --- degenerate ----------------------------------------------------------
    {
        FrameStatsSmoother smoother;
        FrameStats zero;
        // A first frame with dt 0 -- which is what a paused or just-resized
        // editor reports -- must not produce an infinity in the text.
        smoother.update(zero, 0.1f);
        const std::vector<StatsLine> lines = frameStatsLines(zero, budget);
        require(contains(lines, "0.0 fps"), "zero is printed as zero");
        require(contains(lines, "0 batches"), "and so is an empty scene");

        smoother.update(zero, 5.0f); // alpha out of range
        require(smoother.smoothed().frameMs == 0.0f,
                "an alpha above one is clamped rather than overshooting");
        smoother.reset();
        require(smoother.smoothed().batches == 0, "reset clears the average");
    }

    std::cout << "EditorViewportOverlayTests: ok\n";
    return 0;
}
