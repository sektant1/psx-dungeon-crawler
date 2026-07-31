// The pacing shared by the crosshair tooltip and the target banner.
//
// eng::ui::Fade touches nothing but its own fields, so the feel is pinned here
// instead of being eyeballed frame by frame -- which is how it ended up
// strobing in the first place. Both widgets drive one of these, so everything
// below covers both.
#include <eng/ui/UiFade.h>

#include <cmath>
#include <cstdio>

using namespace eng::ui;

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) < eps;
}

constexpr float kDt = 1.0f / 60.0f;

// Advance `seconds` at 60 Hz, wanting the same thing throughout.
static void run(Fade& f, bool wanted, float seconds)
{
    for (float t = 0.0f; t < seconds; t += kDt)
        f.update(wanted, false, kDt);
}

int main()
{
    const FadePace pace; // the shipped defaults are what is under test

    // --- the appear delay -------------------------------------------------
    // Grazing a target for a couple of frames must not flash a panel. This is
    // the fix for the strobe you get sweeping a crosshair across a room.
    {
        Fade f;
        f.configure(pace);
        f.update(true, false, kDt);
        f.update(true, false, kDt);
        check(!f.visible(), "two frames on a target shows nothing");
        check(f.waiting(), "the appear delay is counting down");
        check(nearly(f.alpha(), 0.0f), "and nothing is drawn");
    }
    {
        // Look away during the delay and the countdown resets: a graze must
        // never accumulate into an appearance.
        Fade f;
        f.configure(pace);
        for (int i = 0; i < 5; ++i)
            f.update(true, false, kDt);
        f.update(false, false, kDt);
        check(!f.waiting(), "looking away disarms the delay");
        for (int i = 0; i < 5; ++i)
            f.update(true, false, kDt);
        check(!f.visible(), "a second graze still shows nothing");
    }
    {
        // Held past the delay it appears, and reaches full opacity.
        Fade f;
        f.configure(pace);
        run(f, true, pace.appearDelay + pace.fadeIn + 0.05f);
        check(f.visible(), "held on target, it appears");
        check(nearly(f.alpha(), 1.0f, 0.02f), "and settles at full opacity");
        check(f.travel() == 0, "and has finished travelling");
    }

    // --- easing -----------------------------------------------------------
    // Linear alpha is what read as mechanical; the curve must not be identity.
    {
        Fade f;
        f.configure(pace);
        run(f, true, pace.appearDelay + 0.001f);
        run(f, true, pace.fadeIn * 0.25f);
        check(f.alpha() < f.progress(),
              "the ease starts slower than linear (smoothstep, not identity)");
        check(f.alpha() > 0.0f, "but it has started");
    }

    // --- the travel -------------------------------------------------------
    {
        Fade f;
        f.configure(pace);
        run(f, true, pace.appearDelay + 0.001f);
        check(f.travel() > 0, "it travels into place while fading in");
        check(f.travel() <= pace.rise, "by no more than the styled amount");
    }

    // --- the hold ---------------------------------------------------------
    // A crosshair jittering across a silhouette edge loses the target for a
    // frame or two. That must not start a fade.
    {
        Fade f;
        f.configure(pace);
        run(f, true, pace.appearDelay + pace.fadeIn + 0.05f);
        const float settled = f.alpha();
        f.update(false, false, kDt);
        f.update(false, false, kDt);
        check(nearly(f.alpha(), settled), "a two-frame loss does not dim it");
        f.update(true, false, kDt);
        check(nearly(f.alpha(), settled, 0.02f),
              "re-acquiring resumes instantly, without a second delay");
    }
    {
        // Lost for good: hold, then fade out, then report closed so the owner
        // knows it can drop the content.
        Fade f;
        f.configure(pace);
        run(f, true, pace.appearDelay + pace.fadeIn + 0.05f);
        run(f, false, pace.holdAfterLost + pace.fadeOut + 0.05f);
        check(!f.visible(), "a lost target eventually fades out");
        check(f.closed(), "and reports itself closed");
        check(nearly(f.alpha(), 0.0f), "at zero alpha");
    }
    {
        // Snapping away faster than it arrived is what makes a panel feel like
        // it is flickering rather than closing.
        check(pace.fadeOut > pace.fadeIn,
              "the fade out is gentler than the fade in");
    }

    // --- swapping content --------------------------------------------------
    {
        Fade f;
        f.configure(pace);
        run(f, true, pace.appearDelay + pace.fadeIn + 0.05f);
        check(f.travel() == 0, "settled before the swap");

        f.update(true, /*changed=*/true, kDt);
        check(nearly(f.alpha(), 1.0f, 0.02f),
              "swapping targets does not drop the panel to zero alpha");
        check(f.travel() > 0, "but it does nudge, so the change is visible");

        run(f, true, pace.fadeIn + 0.05f);
        check(f.travel() == 0, "and the nudge decays back to rest");
    }
    {
        // The same target must be perfectly still.
        Fade f;
        f.configure(pace);
        run(f, true, pace.appearDelay + pace.fadeIn + 0.05f);
        f.update(true, /*changed=*/false, kDt);
        check(f.travel() == 0, "the same target does not re-punch");
    }
    {
        // A swap while hidden must not punch: there is nothing on screen to
        // nudge, and it would arrive already displaced.
        Fade f;
        f.configure(pace);
        f.update(true, /*changed=*/true, kDt);
        check(f.travel() == pace.rise,
              "a swap before anything is shown leaves the normal travel");
    }
    {
        // Moving directly between hidden targets restarts the anti-strobe
        // delay; time spent grazing the first target must not count.
        Fade f;
        f.configure(pace);
        for (int i = 0; i < 5; ++i)
            f.update(true, false, kDt);
        f.update(true, /*changed=*/true, kDt);
        for (int i = 0; i < 4; ++i)
            f.update(true, false, kDt);
        check(!f.visible(), "a hidden target swap restarts the appear delay");
    }

    // --- degenerate paces --------------------------------------------------
    // Zero durations mean "instant", not a division by zero.
    {
        FadePace instant;
        instant.appearDelay = 0.0f;
        instant.fadeIn = 0.0f;
        instant.fadeOut = 0.0f;
        instant.holdAfterLost = 0.0f;
        Fade f;
        f.configure(instant);
        f.update(true, false, kDt);
        check(nearly(f.alpha(), 1.0f), "zero fade-in appears immediately");
        f.update(false, false, kDt);
        check(nearly(f.alpha(), 0.0f), "zero fade-out vanishes immediately");
        check(f.closed(), "and closes");
    }

    // Never wanted, never shown, however long.
    {
        Fade f;
        f.configure(pace);
        run(f, false, 1.0f);
        check(!f.visible(), "nothing wanted never appears");
    }

    if (failures == 0) std::printf("TooltipPaceTests OK\n");
    return failures ? 1 : 0;
}
