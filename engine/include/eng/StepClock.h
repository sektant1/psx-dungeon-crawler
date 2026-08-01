#pragma once
#include <cstdint>

namespace eng {

// Which family of motion a step rate applies to.
//
// Separate channels exist because the correct rate is genuinely different per
// family, and because two of these must NOT be stepped together with the rest:
//
//   Characters   The load-bearing channel. Stepping creature motion is what
//                actually reads as stop-motion; everything else is support.
//   Viewmodel    The player's own hands/weapon. Kept separate from Characters
//                because it sits a few centimetres from the eye, where the same
//                step rate reads roughly twice as harsh.
//   World        Props, torch flicker, doors, banners. Free atmosphere: no
//                gameplay rides on it, so it can take the harshest rate.
//   Particles    VFX. Easy to forget and the most common way the illusion
//                breaks: smooth particles next to stepped characters read as a
//                bug rather than a style, because the eye uses the smooth
//                motion as the reference for "what this engine can do".
//   Projectiles  Arrows and spells. Deliberately the fastest channel: these
//                have to stay trackable and dodgeable, and a 12 Hz arrow
//                crossing a room teleports in metre-long jumps.
//
// Deliberately absent, and the rule behind it: step what is *presented*, never
// what is *simulated*. The camera, player locomotion, physics and UI stay
// full-rate. Stepping the camera in first person reads as input lag and makes
// people motion sick within a minute; stepping player movement makes the
// controls feel broken rather than stylised. Physics keeps running at its own
// fixed rate underneath -- a stepped channel only holds the render transform
// between snaps, so collisions and hit registration are unaffected.
enum class StepChannel : int {
    Characters = 0,
    Viewmodel,
    World,
    Particles,
    Projectiles,
    Count
};

inline constexpr int kStepChannelCount = int(StepChannel::Count);

// Human-readable channel name, for debug UI and config keys ("characters", ...).
const char* stepChannelName(StepChannel c);

struct StepRates {
    bool enabled = true;
    // Global multiplier over every rate below, so the whole look can be dialled
    // between "subtle" and "puppet show" with one control. 1 = as authored.
    float scale = 1.0f;

    // Hz per channel. <= 0 means "this channel runs continuous (unstepped)".
    //
    // Classic stop-motion is 24 fps shot on twos -- 12 distinct poses a second
    // -- and 12 Hz is the rate the look is named after. These sit above it:
    // this is a fast game, and at 12 Hz a sprinting enemy or a swinging weapon
    // reads as dropped frames rather than as a deliberate shutter. The
    // viewmodel takes the highest rate because it sits centimetres from the
    // eye, where the same interval is roughly twice as harsh; projectiles stay
    // fastest so they remain trackable and dodgeable.
    float rate[kStepChannelCount] = {
        18.0f, // Characters
        24.0f, // Viewmodel
        15.0f, // World
        18.0f, // Particles
        30.0f, // Projectiles
    };

    // 0 = every object in a channel snaps on the same frame: crisp and
    // deliberate, and correct for a handful of objects on screen.
    // 1 = each object's step phase is spread across the step interval, so a
    // crowd stops moving like one puppet. Costs nothing but makes a channel
    // read as many independent animations instead of one global shutter.
    float phaseJitter = 0.0f;
};

// Quantised animation clock: the engine-wide source of "stop-motion" timing.
//
// Advance it once per frame with the real frame delta, then have each animated
// system ask for its channel's time or delta instead of using the raw one. Two
// accessors because animation comes in two shapes:
//   - integrators (`pos += vel * d`) want delta(), which is 0 on hold frames
//     and one whole step on a snap frame;
//   - pose-from-time (`sin(t * k)`) wants time(), a quantised absolute clock.
// A system that copies a transform rather than computing one (a render sync)
// wants stepped(), and simply skips the copy on hold frames.
//
// Quantised time lags real time by up to one step but never drifts: it is always
// floor(realTime / step) * step, so deltas sum back to the real elapsed time.
class StepClock
{
public:
    const StepRates& rates() const { return mRates; }
    StepRates& rates() { return mRates; } // live-tweakable from debug UI
    void setRates(const StepRates& r) { mRates = r; }
    // Rewinds every channel to zero, keeping the rates. The engine calls this
    // when the loading phase ends.
    //
    // It has to exist because the load loop runs a *variable* number of frames
    // -- it pumps work against a wall-clock millisecond budget, so a slower
    // shader compile means more frames -- and each one advances this clock.
    // Without a rebase, everything stepped (viewmodel, characters, world VFX)
    // started gameplay at a phase that depended on how long the load took, and
    // a "deterministic" capture came out different run to run.
    void rewind();

    // Call once per frame with the real (unquantised) frame delta.
    void advance(float dt);

    // Stepped delta for integrators. Never negative, even across a rate change.
    float delta(StepChannel c) const;
    // Stepped absolute time for pose-from-time animation.
    float time(StepChannel c) const;
    // True on frames where this channel advanced; false on hold frames.
    bool stepped(StepChannel c) const;

    // Per-object phase variants. `seed` is any stable per-object id (entity id,
    // node handle, spawn index). Identical to the shared versions when
    // phaseJitter is 0, so it is safe to call these unconditionally.
    float time(StepChannel c, uint32_t seed) const;
    bool stepped(StepChannel c, uint32_t seed) const;

    // Effective step length in seconds after `scale`, or 0 when the channel is
    // continuous (rate <= 0, or the clock disabled). Useful for debug readouts.
    float stepDuration(StepChannel c) const;

private:
    StepRates mRates;
    // double, not float: this accumulates for the whole session, and float runs
    // out of mantissa where it matters. At an hour in, a float's spacing near
    // 3600 is ~0.25 ms and the quantisation boundaries start landing unevenly,
    // so steps come in visibly irregular clumps. double keeps sub-microsecond
    // resolution for years. The per-channel outputs stay float -- they are
    // deltas and offsets, never large.
    double mRaw = 0.0;     // unquantised accumulated time
    double mPrevRaw = 0.0; // ...as of last frame, for the phase variants
    double mTime[kStepChannelCount] = {};
    float mDelta[kStepChannelCount] = {};
    // Step length actually used last frame, so a rate change mid-run can be
    // detected and absorbed instead of producing a bogus delta.
    float mAppliedStep[kStepChannelCount] = {};
    bool mPrimed = false; // has advance() run at least once (see StepClock.cpp)
};

} // namespace eng
