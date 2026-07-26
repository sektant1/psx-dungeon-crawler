#include "eng/StepClock.h"

#include <algorithm>
#include <cmath>

namespace eng {
namespace {

// Quantise to the step grid: the floor is what makes a pose *hold* and then
// snap, rather than easing toward the next one.
double quantise(double t, double step) { return std::floor(t / step) * step; }

// Cheap integer hash -> [0,1), for per-object step phase.
float hash01(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x >> 8) * (1.0f / 16777216.0f); // top 24 bits
}

} // namespace

const char* stepChannelName(StepChannel c)
{
    switch (c) {
    case StepChannel::Characters:  return "characters";
    case StepChannel::Viewmodel:   return "viewmodel";
    case StepChannel::World:       return "world";
    case StepChannel::Particles:   return "particles";
    case StepChannel::Projectiles: return "projectiles";
    default:                       return "unknown";
    }
}

float StepClock::stepDuration(StepChannel c) const
{
    const int i = int(c);
    if (!mRates.enabled || i < 0 || i >= kStepChannelCount)
        return 0.0f;
    const float hz = mRates.rate[i] * std::max(mRates.scale, 0.0f);
    return hz > 0.0f ? 1.0f / hz : 0.0f; // 0 = continuous
}

void StepClock::advance(float dt)
{
    // Negative frame deltas would rewind every integrator downstream.
    dt = std::max(dt, 0.0f);
    mPrevRaw = mRaw;
    mRaw += dt;

    for (int i = 0; i < kStepChannelCount; ++i) {
        const float step = stepDuration(StepChannel(i));
        const double prev = mTime[i];
        mTime[i] = (step > 0.0f) ? quantise(mRaw, step) : mRaw;

        // A rate change (debug slider, profile swap) moves the quantisation
        // grid, so the difference between old and new quantised time is
        // meaningless and can even be negative. Absorb it: one hold frame is
        // invisible, a rewound projectile is not. Gated on mPrimed because on
        // the very first advance there is no previous rate to differ from --
        // without that check the first frame's delta was always swallowed.
        const bool rateChanged = mPrimed && step != mAppliedStep[i];
        mAppliedStep[i] = step;
        mDelta[i] = rateChanged ? 0.0f
                                : float(std::max(mTime[i] - prev, 0.0));
    }
    mPrimed = true;
}

float StepClock::delta(StepChannel c) const
{
    const int i = int(c);
    return (i >= 0 && i < kStepChannelCount) ? mDelta[i] : 0.0f;
}

float StepClock::time(StepChannel c) const
{
    const int i = int(c);
    return (i >= 0 && i < kStepChannelCount) ? float(mTime[i]) : 0.0f;
}

bool StepClock::stepped(StepChannel c) const
{
    const int i = int(c);
    if (i < 0 || i >= kStepChannelCount)
        return true;
    // A continuous channel "steps" every frame, so callers that gate a render
    // sync on this keep working unchanged when stepping is switched off.
    return stepDuration(c) <= 0.0f ? true : mDelta[i] > 0.0f;
}

float StepClock::time(StepChannel c, uint32_t seed) const
{
    const float step = stepDuration(c);
    if (step <= 0.0f || mRates.phaseJitter <= 0.0f)
        return time(c);
    // Offset into the grid, quantise, then take the offset back out: the result
    // advances on the same interval as the shared clock but snaps at a moment
    // unique to this object.
    const double off = hash01(seed) * std::min(mRates.phaseJitter, 1.0f) * step;
    return float(quantise(mRaw + off, step) - off);
}

bool StepClock::stepped(StepChannel c, uint32_t seed) const
{
    const float step = stepDuration(c);
    if (step <= 0.0f || mRates.phaseJitter <= 0.0f)
        return stepped(c);
    const double off = hash01(seed) * std::min(mRates.phaseJitter, 1.0f) * step;
    return quantise(mRaw + off, step) != quantise(mPrevRaw + off, step);
}

} // namespace eng
