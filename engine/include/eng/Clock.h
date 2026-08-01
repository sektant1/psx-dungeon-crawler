#pragma once
#include <cstdint>

namespace eng {

// An abstract timeline (Game Engine Architecture, 4th ed., 8.4-8.5): a clock
// that advances from the real frame delta but does not have to agree with it.
//
// Everything that simulates should read a Clock rather than the wall delta,
// because the two answer different questions. The wall delta says "how long did
// that frame take"; a game clock says "how much game happened". Once they are
// separate, three things become one line of code each:
//
//   pause        stop advancing the game clock. The loop keeps running, so the
//                renderer, the debug camera and the UI stay live over a frozen
//                world -- which is what makes a visual bug inspectable.
//   slow motion  scale < 1 for a hit-stop or a death cam; scale > 1 for a
//                fast-forward. Nothing downstream knows it happened.
//   single step  advance the game clock by exactly one frame while paused, to
//                walk a spawn or an impact frame by frame. Not the same as a
//                breakpoint in the loop: the rest of the engine keeps drawing.
//
// The engine owns two of these. The *game* clock drives simulation and is the
// one that pauses; the *real* clock never pauses and never scales, and is what
// the debug UI, the frame limiter and anything that must keep moving while the
// world is frozen should use.
//
// elapsed() is double for the same reason StepClock's accumulator is: a float
// runs out of mantissa a couple of hours in and time starts advancing in
// visible clumps. Deltas stay float -- they are always small.
class Clock
{
public:
    explicit Clock(double startSeconds = 0.0) : mElapsed(startSeconds) {}

    // Advance by one frame. `realDt` is the unscaled wall delta; what actually
    // lands on the timeline is realDt * scale, or one single-step's worth while
    // paused, or nothing.
    void update(float realDt);

    // --- controls --------------------------------------------------------
    // 1 = real time. Clamped at 0: a negative scale would run integrators
    // backwards, and nothing downstream in this engine survives that.
    void setScale(float s);
    float scale() const { return mScale; }

    void setPaused(bool p) { mPaused = p; }
    bool paused() const { return mPaused; }
    void togglePause() { mPaused = !mPaused; }

    // Advance one frame's worth on the next update() even though paused. Held
    // until that update consumes it, so a keypress handled anywhere in the
    // frame still produces exactly one step.
    void requestSingleStep() { mStepPending = true; }
    // What one single step is worth. Defaults to a 60 Hz frame.
    void setSingleStepDuration(float seconds) { mStepDuration = seconds; }
    float singleStepDuration() const { return mStepDuration; }

    // --- readings --------------------------------------------------------
    // Time this timeline has accumulated. Not wall time unless scale has been 1
    // and the clock never paused.
    double elapsed() const { return mElapsed; }
    // What the last update() added. Zero on a paused frame, which is what makes
    // "pass clock.delta() to the simulation" the whole implementation of pause.
    float delta() const { return mDelta; }
    // update() calls so far, paused ones included: this counts *frames*, not
    // time, and stays useful as a frame index while the world is frozen.
    std::uint64_t frame() const { return mFrame; }

    // Difference between two timelines, in this clock's units. The book's
    // canonical use is mapping a local timeline (an animation clip, an ability
    // cooldown) onto a global one without either side knowing the other's
    // origin or rate.
    float deltaTo(const Clock& other) const
    {
        return float(other.mElapsed - mElapsed);
    }

    // Rewind to `seconds` and drop any pending step, keeping scale and pause
    // state. The engine calls this when the loading phase ends so gameplay
    // starts at t=0 no matter how long the load took -- the same reason
    // StepClock::rewind() exists.
    void reset(double seconds = 0.0);

private:
    double mElapsed = 0.0;
    float mDelta = 0.0f;
    float mScale = 1.0f;
    float mStepDuration = 1.0f / 60.0f;
    std::uint64_t mFrame = 0;
    bool mPaused = false;
    bool mStepPending = false;
};

} // namespace eng
