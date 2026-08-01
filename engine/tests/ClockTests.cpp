#include <eng/Clock.h>

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace eng;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "ClockTests: " << m << '\n';
        std::exit(1);
    }
}

static bool near(double a, double b, double eps = 1e-5)
{
    return std::fabs(a - b) <= eps;
}

int main()
{
    // Unscaled, unpaused: the game timeline is the real timeline.
    {
        Clock c;
        for (int i = 0; i < 60; ++i)
            c.update(1.0f / 60.0f);
        require(near(c.elapsed(), 1.0), "60 frames of 1/60 s is one second");
        require(near(c.delta(), 1.0 / 60.0), "delta is the frame's own delta");
        require(c.frame() == 60, "frame counts updates");
    }

    // Scale warps the timeline without anything downstream knowing.
    {
        Clock c;
        c.setScale(0.25f); // hit-stop
        c.update(0.1f);
        require(near(c.delta(), 0.025), "quarter speed quarters the delta");
        c.setScale(4.0f); // fast-forward
        c.update(0.1f);
        require(near(c.delta(), 0.4), "4x speed quadruples the delta");
        require(near(c.elapsed(), 0.425), "elapsed accumulates scaled time");

        // A negative scale would run every integrator backwards; clamped, not
        // honoured.
        c.setScale(-2.0f);
        require(c.scale() == 0.0f, "negative scale clamps to frozen");
        c.update(0.1f);
        require(c.delta() == 0.0f, "scale 0 stops the timeline");
    }

    // Pause stops the timeline but not the frame counter: the loop is still
    // running, which is the whole point (the renderer keeps painting).
    {
        Clock c;
        c.update(0.016f);
        const double t = c.elapsed();
        c.setPaused(true);
        c.update(0.016f);
        c.update(0.016f);
        require(c.delta() == 0.0f, "paused frames advance nothing");
        require(near(c.elapsed(), t), "paused time does not accumulate");
        require(c.frame() == 3, "frames still count while paused");

        // Single step: exactly one step's worth, exactly once, and its size is
        // the configured step rather than the (usually slow) frame it happened
        // on -- otherwise stepping through a bug is not reproducible.
        c.setSingleStepDuration(1.0f / 30.0f);
        c.requestSingleStep();
        c.update(0.5f);
        require(near(c.delta(), 1.0 / 30.0), "single step is the fixed size");
        c.update(0.5f);
        require(c.delta() == 0.0f, "the step is consumed, not repeated");

        c.togglePause();
        require(!c.paused(), "toggle unpauses");
        c.update(0.016f);
        require(near(c.delta(), 0.016), "resumed clock runs at real time again");
    }

    // A negative wall delta (per-core timer drift, the book's warning in 8.5.3.1)
    // must never reach an integrator.
    {
        Clock c;
        c.update(-1.0f);
        require(c.delta() == 0.0f, "negative delta clamps to zero");
        require(c.elapsed() == 0.0, "and does not rewind elapsed time");
    }

    // Mapping one timeline onto another: what a clip or a cooldown needs.
    {
        Clock game;
        Clock local(10.0);
        game.update(0.5f);
        require(near(game.deltaTo(local), 9.5), "offset between two timelines");
    }

    // reset() rebases without losing the controls, the way the engine rebases
    // at the end of the loading phase.
    {
        Clock c;
        c.setScale(0.5f);
        c.update(1.0f);
        c.setPaused(true);
        c.requestSingleStep(); // still pending when the rebase happens
        c.reset();
        require(c.elapsed() == 0.0, "reset rewinds");
        require(c.frame() == 0, "reset restarts the frame count");
        require(c.paused() && c.scale() == 0.5f, "reset keeps the controls");
        c.update(1.0f);
        require(c.delta() == 0.0f, "reset drops a pending single step");
    }

    std::cout << "ClockTests OK\n";
    return 0;
}
