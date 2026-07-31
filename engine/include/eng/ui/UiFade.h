#pragma once

namespace eng::ui {

// How a transient HUD element comes and goes.
//
// Every one of these numbers exists because of a specific way the UI read as
// broken without it; the comments say which. Shared by the crosshair tooltip
// and the target banner so the two cannot drift into different feels.
struct FadePace {
    // Sweeping a crosshair across a room grazes a dozen targets. With no
    // delay each one popped a panel for two frames and the screen strobed.
    float appearDelay = 0.12f;
    float fadeIn = 0.13f;
    // Held briefly after the target is lost, so a crosshair jittering across a
    // silhouette edge does not flicker the panel off and back on.
    float holdAfterLost = 0.07f;
    // Gentler than the fade in: snapping away faster than it arrived is what
    // makes a panel feel like it is flickering rather than closing.
    float fadeOut = 0.20f;
    // Travels this many virtual pixels into place while fading in. Small on
    // purpose: enough to read as arriving, not enough to be a slide.
    int rise = 5;
    // A content swap while the element is up replays this fraction of the
    // travel, so a change reads as a change rather than a silent text edit.
    float swapPunch = 0.45f;
};

// The pacing state machine, with no drawing and no content of its own.
//
// It is a pure function of (wanted, changed, dt) over its own fields, which is
// what lets the feel be pinned by tests instead of eyeballed frame by frame --
// and what lets two very different-looking widgets share one behaviour.
class Fade {
public:
    void configure(const FadePace& pace) { mPace = pace; }
    const FadePace& pace() const { return mPace; }

    // `wanted`: is there something to show this frame.
    // `changed`: it is something *different* from what is already showing.
    void update(bool wanted, bool changed, float dt);

    // Eased 0..1. Linear alpha reads mechanical; this is smoothstepped so the
    // element settles rather than arriving at constant speed.
    float alpha() const;
    // Travel offset in virtual pixels: full at alpha 0, zero once settled,
    // plus whatever a recent swap punched back in.
    int travel() const;
    bool visible() const { return mProgress > 0.001f; }
    // True once the fade has fully closed, so the owner knows it is safe to
    // drop the content it was showing.
    bool closed() const { return mProgress <= 0.0f; }

    // For tests: raw linear progress, and whether the appear delay is running.
    float progress() const { return mProgress; }
    bool waiting() const { return mDelay > 0.0f; }

private:
    FadePace mPace;
    float mProgress = 0.0f; // 0..1, linear; eased on read
    float mDelay = 0.0f;    // counts up to appearDelay before showing
    float mHold = 0.0f;     // counts down before a shown element fades
    float mSwap = 0.0f;     // 0..1, decays; replays part of the travel
};

} // namespace eng::ui
