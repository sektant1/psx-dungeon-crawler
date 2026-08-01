#include <eng/Clock.h>

#include <algorithm>

namespace eng {

void Clock::setScale(float s) { mScale = std::max(s, 0.0f); }

void Clock::update(float realDt)
{
    ++mFrame;
    // A negative wall delta is not physically possible but is observable: the
    // book warns that per-core high-resolution timers drift and can produce one.
    // Clamping here means no downstream integrator ever sees time run backwards.
    realDt = std::max(realDt, 0.0f);

    if (mPaused) {
        // A pending step outranks the pause for exactly one frame, and is
        // deliberately a fixed duration rather than realDt: stepping frame by
        // frame through a bug is only reproducible if every step is the same
        // size, and the frame you are staring at is usually a slow one.
        if (mStepPending) {
            mStepPending = false;
            mDelta = mStepDuration;
        } else {
            mDelta = 0.0f;
        }
    } else {
        mDelta = realDt * mScale;
    }

    mElapsed += double(mDelta);
}

void Clock::reset(double seconds)
{
    mElapsed = seconds;
    mDelta = 0.0f;
    mFrame = 0;
    mStepPending = false;
}

} // namespace eng
