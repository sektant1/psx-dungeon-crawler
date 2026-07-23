#pragma once
#include <eng/Ease.h>
#include <functional>
#include <memory>
#include <glm/glm.hpp>

namespace eng {

// Base of every schedulable action. update() advances by dt and returns the
// leftover dt once it finishes this frame (0 while still running), so a
// Sequence can chain instantaneous actions within one frame.
class Action {
public:
    virtual ~Action() = default;
    virtual float update(float dt) = 0;
    bool finished() const { return mFinished; }
protected:
    bool mFinished = false;
};

using ActionPtr = std::unique_ptr<Action>;

// Waits `duration` seconds.
class ActionDelay : public Action {
public:
    explicit ActionDelay(float duration) : mDuration(duration) {}
    float update(float dt) override {
        mElapsed += dt;
        if (mElapsed >= mDuration) { mFinished = true; return mElapsed - mDuration; }
        return 0.0f;
    }
private:
    float mDuration;
    float mElapsed = 0.0f;
};

// Invokes a callback once, then finishes immediately (passes all dt through).
class ActionCall : public Action {
public:
    explicit ActionCall(std::function<void()> fn) : mFn(std::move(fn)) {}
    float update(float dt) override {
        if (mFn) mFn();
        mFinished = true;
        return dt;
    }
private:
    std::function<void()> mFn;
};

// Linear-space interpolation helpers used by ActionProperty.
inline float          lerpProp(float a, float b, float t)          { return a + (b - a) * t; }
inline glm::vec2 lerpProp(const glm::vec2& a, const glm::vec2& b, float t) { return glm::mix(a, b, t); }
inline glm::vec3 lerpProp(const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); }
inline glm::vec4 lerpProp(const glm::vec4& a, const glm::vec4& b, float t) { return glm::mix(a, b, t); }

// Tweens a referenced property from its live value (captured on first update)
// to `target` over `duration` seconds using `ease`. The referenced variable
// MUST outlive the owning action set; free the tween via Actions::clear() (or
// let it finish) before destroying the target.
template <typename T>
class ActionProperty : public Action {
public:
    ActionProperty(T& prop, T target, float duration, Ease ease)
        : mProp(prop), mEnd(std::move(target)), mDuration(duration), mEase(ease) {}
    float update(float dt) override {
        if (!mStarted) { mStart = mProp; mStarted = true; }
        mElapsed += dt;
        float t = mDuration > 0.0f ? mElapsed / mDuration : 1.0f;
        if (t >= 1.0f) { t = 1.0f; mFinished = true; }
        mProp = lerpProp(mStart, mEnd, easeApply(t, mEase));
        return mFinished ? (mElapsed - mDuration) : 0.0f;
    }
private:
    T& mProp;
    T mStart{};
    T mEnd;
    float mDuration;
    float mElapsed = 0.0f;
    bool mStarted = false;
    Ease mEase;
};

} // namespace eng
