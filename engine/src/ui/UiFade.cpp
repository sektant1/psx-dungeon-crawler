#include <eng/ui/UiFade.h>

#include <algorithm>
#include <cmath>

namespace {

float smoothstep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

namespace eng::ui {

float Fade::alpha() const { return smoothstep(mProgress); }

int Fade::travel() const {
    // The swap punch rides on top of the settle, so a content change nudges
    // the element even when it is already fully faded in.
    const float settle = 1.0f - alpha();
    const float total = std::min(1.0f, settle + mSwap);
    return int(std::lround(total * float(mPace.rise)));
}

void Fade::update(bool wanted, bool changed, float dt) {
    if (wanted && changed && mProgress <= 0.0f)
        mDelay = 0.0f;
    if (wanted) {
        if (changed && mProgress > 0.0f)
            mSwap = std::clamp(mPace.swapPunch, 0.0f, 1.0f);
        mHold = mPace.holdAfterLost;
    }

    // The swap punch decays on its own clock; it is a nudge, not a state.
    if (mSwap > 0.0f)
        mSwap = std::max(0.0f, mSwap - dt / std::max(0.01f, mPace.fadeIn));

    if (wanted) {
        // Arm the delay only from rest. Something already on screen (or on its
        // way out) must snap back without re-waiting, or recovering from a
        // momentary loss of the target costs a second appear delay.
        if (mProgress <= 0.0f) {
            if (mDelay < mPace.appearDelay) {
                mDelay += dt;
                return;
            }
        } else {
            mDelay = mPace.appearDelay; // already shown: stay armed
        }
        mProgress = std::min(
            1.0f, mProgress + (mPace.fadeIn > 0.0f ? dt / mPace.fadeIn : 1.0f));
        return;
    }

    // Not wanted: hold briefly, then fade.
    mDelay = 0.0f;
    if (mHold > 0.0f) {
        mHold -= dt;
        return;
    }
    mProgress = std::max(
        0.0f, mProgress - (mPace.fadeOut > 0.0f ? dt / mPace.fadeOut : 1.0f));
    if (mProgress <= 0.0f)
        mSwap = 0.0f;
}

} // namespace eng::ui
