#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct ImDrawList;

namespace ed {

// The readout that sits in the corner of the 3D view.
//
// A level editor that cannot tell you what a room costs is a level editor that
// lets you build a room the game cannot draw. The numbers were in the status
// bar, at the bottom of the window, in the same grey as the file name -- which
// is to say nobody watched them while placing geometry, which is the only
// moment they mean anything.
//
// Smoothed, because a raw per-frame millisecond count flickers through three
// digits and reads as noise. The counts are not smoothed: a batch count that
// lags a placement is worse than useless, it is wrong.
struct FrameStats {
    float fps = 0.0f;
    float frameMs = 0.0f;
    std::size_t batches = 0;
    std::size_t triangles = 0;
    std::size_t entities = 0;
    std::size_t selected = 0;
};

// Exponential moving average over the timings. `alpha` is how much of the new
// sample to take: 0.1 settles in about twenty frames, which is slow enough to
// read and fast enough to notice a stall.
class FrameStatsSmoother
{
public:
    void update(const FrameStats& sample, float alpha);
    const FrameStats& smoothed() const { return mSmoothed; }
    void reset() { mSmoothed = FrameStats{}; }

private:
    FrameStats mSmoothed;
};

// A budget the readout colours against. Not a hard limit -- the editor draws
// more than the game does, and an author is allowed to exceed it while working
// -- but a room that is already over it in the editor will not come back under
// it in the game.
struct FrameBudget {
    float frameMs = 16.7f;      // 60 fps
    std::size_t batches = 400;  // what this renderer holds without stuttering
    std::size_t triangles = 120000;
};

// Draws the readout at the top-right of the rect. `origin` and `size` are the
// viewport image in window pixels.
void drawFrameStats(ImDrawList* list, const FrameStats& stats,
                    const FrameBudget& budget, float originX, float originY,
                    float sizeX, float sizeY);

// One line of the readout, formatted and coloured. Split out because the
// formatting is the part worth pinning: "0 batches" and "-1 batches" look the
// same at a glance and mean very different things.
struct StatsLine {
    std::string text;
    bool overBudget = false;
};
std::vector<StatsLine> frameStatsLines(const FrameStats& stats,
                                       const FrameBudget& budget);

} // namespace ed
