#include "eng/StepClock.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using eng::StepChannel;
using eng::StepClock;
using eng::StepRates;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "StepClockTests: " << message << '\n';
        std::exit(1);
    }
}

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

// A clock with every channel at `hz` (0 = continuous).
StepClock makeClock(float hz, bool enabled = true, float jitter = 0.0f)
{
    StepClock c;
    StepRates r;
    r.enabled = enabled;
    r.phaseJitter = jitter;
    for (int i = 0; i < eng::kStepChannelCount; ++i)
        r.rate[i] = hz;
    c.setRates(r);
    return c;
}

// 1/64 s, and rates below are powers of two, so every quantisation boundary
// lands exactly on a frame. Using 1/60 and 10 Hz instead would make the exact
// step counts hostage to float rounding (60 frames of float(1/60) sum to just
// under a second, so the 10th step legitimately does not land) and the test
// would be asserting IEEE behaviour rather than this class's.
constexpr float kFrame = 1.0f / 64.0f;
constexpr float kHz = 8.0f;        // step 0.125 s = every 8th frame
constexpr int kFramesPerSec = 64;

} // namespace

int main()
{
    // --- disabled: a pass-through, so switching stepping off cannot change any
    // caller's behaviour ------------------------------------------------------
    {
        StepClock c = makeClock(12.0f, /*enabled=*/false);
        for (int i = 0; i < 10; ++i) {
            c.advance(kFrame);
            require(near(c.delta(StepChannel::Characters), kFrame),
                    "disabled clock must pass dt through unchanged");
            require(c.stepped(StepChannel::Characters),
                    "disabled clock must report every frame as a step");
        }
        require(near(c.time(StepChannel::Characters), 10.0f * kFrame),
                "disabled clock time must equal real elapsed time");
    }

    // --- rate 0 means "this channel is continuous" --------------------------
    {
        StepClock c = makeClock(0.0f);
        c.advance(kFrame);
        require(near(c.delta(StepChannel::World), kFrame),
                "rate 0 must run continuous");
        require(c.stepped(StepChannel::World),
                "a continuous channel steps every frame, so render-sync gates "
                "keep working when stepping is off");
        require(near(c.stepDuration(StepChannel::World), 0.0f),
                "continuous channel reports no step duration");
    }

    // --- the core behaviour: hold, then snap by one whole step --------------
    {
        StepClock c = makeClock(kHz);
        int stepFrames = 0, holdFrames = 0;
        for (int i = 1; i <= kFramesPerSec; ++i) {
            c.advance(kFrame);
            const float d = c.delta(StepChannel::Characters);
            if (c.stepped(StepChannel::Characters)) {
                ++stepFrames;
                require(near(d, 1.0f / kHz),
                        "a step frame advances one whole step");
            } else {
                ++holdFrames;
                require(near(d, 0.0f), "a hold frame advances nothing");
            }
        }
        require(stepFrames == int(kHz), "channel must snap at its rate");
        require(holdFrames == kFramesPerSec - int(kHz),
                "the remaining frames must hold");
    }

    // --- no drift: quantised time trails real time by under one step, forever
    {
        StepClock c = makeClock(12.0f);
        float real = 0.0f, summed = 0.0f;
        // Deliberately uneven frame times, including a long hitch, to make sure
        // the accumulator neither loses nor invents time.
        const float dts[] = {kFrame, 0.004f, 0.031f, kFrame, 0.09f, 0.001f};
        for (int i = 0; i < 500; ++i) {
            const float dt = dts[i % 6];
            real += dt;
            c.advance(dt);
            summed += c.delta(StepChannel::Characters);
        }
        const float t = c.time(StepChannel::Characters);
        require(near(summed, t, 1e-3f),
                "deltas must sum to the quantised clock");
        require(t <= real + 1e-6f, "quantised time must never run ahead");
        require(real - t < 1.0f / 12.0f + 1e-4f,
                "quantised time must trail real time by less than one step");
    }

    // --- a long frame releases every step it covered, not just one ----------
    {
        StepClock c = makeClock(10.0f);
        c.advance(0.35f); // 3.5 steps' worth
        require(near(c.delta(StepChannel::Characters), 0.3f),
                "a long frame must release all whole steps it spans");
    }

    // --- channels are independent: projectiles outrun characters -----------
    {
        StepClock c;
        StepRates r;
        r.rate[int(StepChannel::Characters)] = 8.0f;
        r.rate[int(StepChannel::Projectiles)] = 32.0f;
        c.setRates(r);
        int chSteps = 0, prSteps = 0;
        for (int i = 0; i < kFramesPerSec; ++i) {
            c.advance(kFrame);
            chSteps += c.stepped(StepChannel::Characters) ? 1 : 0;
            prSteps += c.stepped(StepChannel::Projectiles) ? 1 : 0;
        }
        require(chSteps == 8, "characters channel snaps at its own rate");
        require(prSteps == 32, "projectiles channel snaps at its own rate");
    }

    // --- scale multiplies every channel ------------------------------------
    {
        StepClock c = makeClock(10.0f);
        c.rates().scale = 2.0f;
        require(near(c.stepDuration(StepChannel::World), 1.0f / 20.0f),
                "scale must multiply the effective rate");
    }

    // --- retuning the rate live must never emit a negative delta -----------
    // A rate change moves the quantisation grid, so the naive difference between
    // old and new quantised time can go backwards -- which would rewind every
    // integrator downstream (projectiles flying in reverse for a frame).
    {
        StepClock c = makeClock(12.0f);
        for (int i = 0; i < 20; ++i)
            c.advance(kFrame);
        for (float hz : {31.0f, 7.0f, 60.0f, 9.0f, 12.0f}) {
            for (int i = 0; i < eng::kStepChannelCount; ++i)
                c.rates().rate[i] = hz;
            c.advance(kFrame);
            for (int i = 0; i < eng::kStepChannelCount; ++i)
                require(c.delta(StepChannel(i)) >= 0.0f,
                        "a rate change must never produce a negative delta");
            // ...and normal stepping resumes afterwards.
            c.advance(kFrame);
            require(c.delta(StepChannel::Characters) >= 0.0f,
                    "deltas stay non-negative after a rate change");
        }
    }

    // --- a negative frame delta cannot rewind the clock --------------------
    {
        StepClock c = makeClock(12.0f);
        c.advance(0.5f);
        const float before = c.time(StepChannel::Characters);
        c.advance(-1.0f);
        require(c.time(StepChannel::Characters) >= before,
                "a negative dt must not rewind quantised time");
        require(c.delta(StepChannel::Characters) >= 0.0f,
                "a negative dt must not produce a negative delta");
    }

    // --- phase jitter: same rate, different snap frames per object ---------
    {
        // jitter 0: every object shares one shutter.
        StepClock plain = makeClock(kHz, true, 0.0f);
        for (int i = 0; i < 30; ++i) {
            plain.advance(kFrame);
            require(plain.stepped(StepChannel::Characters, 1u) ==
                        plain.stepped(StepChannel::Characters, 999u),
                    "with jitter 0 all objects must snap together");
            require(near(plain.time(StepChannel::Characters, 7u),
                         plain.time(StepChannel::Characters)),
                    "with jitter 0 the seeded time equals the shared time");
        }

        // jitter 1: two seeds must disagree at least sometimes, and each must
        // still snap exactly at its channel rate (jitter shifts phase, never
        // frequency).
        StepClock jit = makeClock(kHz, true, 1.0f);
        int disagreements = 0, aSteps = 0, bSteps = 0;
        const int frames = kFramesPerSec * 2; // 2 seconds
        for (int i = 0; i < frames; ++i) {
            jit.advance(kFrame);
            const bool a = jit.stepped(StepChannel::Characters, 11u);
            const bool b = jit.stepped(StepChannel::Characters, 4242u);
            disagreements += (a != b) ? 1 : 0;
            aSteps += a ? 1 : 0;
            bSteps += b ? 1 : 0;
        }
        require(disagreements > 0,
                "phase jitter must desynchronise objects' snap frames");
        require(aSteps == int(kHz) * 2 && bSteps == int(kHz) * 2,
                "jitter shifts phase, so each object still snaps at the "
                "channel rate");
    }

    // --- rewind ---------------------------------------------------------
    // The engine rebases this clock when the loading phase ends. The load loop
    // runs a *variable* number of frames -- it pumps work against a wall-clock
    // millisecond budget, so a slower shader compile means more frames -- and
    // each one advanced the clock. Without the rebase, every stepped system
    // started gameplay at a phase that depended on how long the load took, and
    // a capture pinned to a frame number came out different run to run (64k
    // differing pixels between two runs of the same binary; zero after).
    {
        StepClock a, b;
        StepRates rates;
        rates.enabled = true;
        for (int i = 0; i < eng::kStepChannelCount; ++i)
            rates.rate[i] = kHz;
        a.setRates(rates);
        b.setRates(rates);

        // Two clocks that ran a different number of "loading" frames.
        for (int i = 0; i < 7; ++i)
            a.advance(kFrame);
        for (int i = 0; i < 23; ++i)
            b.advance(kFrame);
        require(a.time(StepChannel::Viewmodel) != b.time(StepChannel::Viewmodel),
                "the fixture must actually diverge before the rebase");

        a.rewind();
        b.rewind();
        require(a.time(StepChannel::Viewmodel) == 0.0f &&
                    b.time(StepChannel::Viewmodel) == 0.0f,
                "rewind zeroes every channel");
        require(a.delta(StepChannel::Viewmodel) == 0.0f,
                "rewind leaves no delta from the discarded time");

        // From here the two must agree forever: this is the property the
        // deterministic capture actually depends on.
        for (int i = 0; i < kFramesPerSec; ++i) {
            a.advance(kFrame);
            b.advance(kFrame);
            for (int c = 0; c < eng::kStepChannelCount; ++c) {
                const StepChannel ch = StepChannel(c);
                require(a.time(ch) == b.time(ch),
                        "rebased clocks must stay in phase");
                require(a.stepped(ch) == b.stepped(ch),
                        "rebased clocks must snap on the same frames");
            }
        }
        // And the rates survive the rebase -- it rewinds time, not tuning.
        require(a.stepDuration(StepChannel::Viewmodel) ==
                    b.stepDuration(StepChannel::Viewmodel),
                "rewind keeps the configured rates");
    }

    std::cout << "StepClockTests: OK\n";
    return 0;
}
