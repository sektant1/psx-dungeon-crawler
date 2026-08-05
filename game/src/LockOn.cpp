#include "LockOn.h"

#include <algorithm>
#include <cmath>

namespace game {
namespace {

// Signed angle of `point` about the view's up axis, relative to straight ahead.
// Positive is to the right. What "the next target to the left" is measured in.
float bearing(glm::vec3 eye, glm::vec3 forward, glm::vec3 point)
{
    const glm::vec3 flatForward =
        glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    const glm::vec3 right(-flatForward.z, 0.0f, flatForward.x);
    const glm::vec3 delta = point - eye;
    return std::atan2(glm::dot(delta, right), glm::dot(delta, flatForward));
}

} // namespace

const LockCandidate* acquireLockTarget(const std::vector<LockCandidate>& all,
                                       glm::vec3 eye, glm::vec3 forward,
                                       const LockOnTuning& tuning)
{
    if (glm::length(forward) < 1e-4f)
        return nullptr;
    const glm::vec3 aim = glm::normalize(forward);
    const float cosLimit = std::cos(glm::radians(
        std::clamp(tuning.acquireConeDegrees, 1.0f, 179.0f)));

    const LockCandidate* best = nullptr;
    float bestScore = -1.0f;
    for (const LockCandidate& candidate : all) {
        const glm::vec3 delta = candidate.point - eye;
        const float distance = glm::length(delta);
        if (distance < 1e-3f || distance > tuning.acquireRange)
            continue;
        const float alignment = glm::dot(delta / distance, aim);
        if (alignment < cosLimit)
            continue;
        // Alignment first, distance only as a tie-break: two enemies the same
        // few degrees apart should resolve by which is closer, but a distant
        // one dead ahead still beats a near one at the edge of the cone.
        const float score =
            alignment - 0.06f * (distance / std::max(tuning.acquireRange, 0.01f));
        if (!best || score > bestScore) {
            best = &candidate;
            bestScore = score;
        }
    }
    return best;
}

const LockCandidate* switchLockTarget(const std::vector<LockCandidate>& all,
                                      int currentId, glm::vec3 eye,
                                      glm::vec3 forward, float direction,
                                      const LockOnTuning& tuning)
{
    if (direction == 0.0f || glm::length(forward) < 1e-4f)
        return nullptr;
    const LockCandidate* current = nullptr;
    for (const LockCandidate& candidate : all)
        if (candidate.id == currentId)
            current = &candidate;
    const float from = current ? bearing(eye, forward, current->point) : 0.0f;
    const float sign = direction > 0.0f ? 1.0f : -1.0f;

    // The nearest candidate strictly on the requested side, by bearing. A small
    // dead band keeps two enemies at the same bearing from swapping every flick.
    constexpr float kDeadBand = 0.02f; // radians
    const LockCandidate* best = nullptr;
    float bestDelta = 0.0f;
    for (const LockCandidate& candidate : all) {
        if (candidate.id == currentId)
            continue;
        const float distance = glm::length(candidate.point - eye);
        if (distance < 1e-3f || distance > tuning.breakRange)
            continue;
        const float delta = (bearing(eye, forward, candidate.point) - from) * sign;
        if (delta <= kDeadBand)
            continue;
        if (!best || delta < bestDelta) {
            best = &candidate;
            bestDelta = delta;
        }
    }
    return best;
}

void LockOnSystem::clear()
{
    mLocked = false;
    mTargetId = -1;
    mUnseenFor = 0.0f;
    mSwitchTravel = 0.0f;
}

eng::CameraLockOn LockOnSystem::camera() const
{
    eng::CameraLockOn lock;
    lock.active = mLocked;
    lock.point = mPoint;
    lock.radius = mRadius;
    return lock;
}

bool LockOnSystem::targetPoint(glm::vec3& out) const
{
    if (!mLocked)
        return false;
    out = mPoint;
    return true;
}

void LockOnSystem::update(const std::vector<LockCandidate>& candidates,
                          glm::vec3 eye, glm::vec3 forward, const Input& input,
                          float dt, const VisibilityTest& visible)
{
    if (!input.enabled) {
        // A menu owning the input is not a reason to lose the lock, but it is
        // a reason to stop steering it.
        return;
    }

    if (input.togglePressed) {
        if (mLocked) {
            clear();
        } else if (const LockCandidate* target =
                       acquireLockTarget(candidates, eye, forward, mTuning)) {
            mLocked = true;
            mTargetId = target->id;
            mPoint = target->point;
            mRadius = target->radius;
            mUnseenFor = 0.0f;
            mSwitchTravel = 0.0f;
        }
        // Pressing with nothing in the cone does nothing, deliberately: the
        // alternative -- locking onto whatever is nearest -- is how a lock ends
        // up on the corpse behind you.
    }
    if (!mLocked)
        return;

    // Switching. The travel decays so a slow drag across the screen never
    // accumulates into a switch; only a flick does.
    mSwitchTravel += input.switchAxis;
    mSwitchTravel *= std::exp(-4.0f * std::max(dt, 0.0f));
    if (std::abs(mSwitchTravel) >= mTuning.switchThresholdPixels) {
        // Mouse +x moves the view right, so a rightward flick asks for the
        // target to the right.
        const float direction = mSwitchTravel > 0.0f ? 1.0f : -1.0f;
        if (const LockCandidate* next = switchLockTarget(
                candidates, mTargetId, eye, forward, direction, mTuning)) {
            mTargetId = next->id;
            mPoint = next->point;
            mRadius = next->radius;
            mUnseenFor = 0.0f;
        }
        mSwitchTravel = 0.0f;
    }

    // Keep, or drop. Four ways to lose a lock, and each of them is a thing the
    // player can see happen: the target died (it is no longer a candidate), it
    // ran, it went behind something, or they let it go.
    const LockCandidate* held = nullptr;
    for (const LockCandidate& candidate : candidates)
        if (candidate.id == mTargetId)
            held = &candidate;
    if (!held) {
        clear();
        return;
    }
    mPoint = held->point;
    mRadius = held->radius;
    if (glm::length(mPoint - eye) > mTuning.breakRange) {
        clear();
        return;
    }
    if (visible && !visible(eye, mPoint)) {
        mUnseenFor += std::max(dt, 0.0f);
        if (mUnseenFor > mTuning.occlusionGrace)
            clear();
    } else {
        mUnseenFor = 0.0f;
    }
}

} // namespace game
