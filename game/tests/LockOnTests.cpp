// The lock-on's decisions: what it grabs, what it refuses, what makes it let
// go. All of it is pure or nearly so, which is the point of keeping the rules
// in game::LockOnSystem rather than in the camera that frames the result.

#include "LockOn.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    if (!condition) {
        std::fprintf(stderr, "LockOnTests: %s\n", what);
        ++failures;
    }
}

const glm::vec3 kEye{0.0f, 1.6f, 0.0f};
const glm::vec3 kForward{0.0f, 0.0f, -1.0f}; // yaw 0 looks down -Z

std::vector<game::LockCandidate> scene()
{
    return {
        {1, {0.0f, 1.0f, -8.0f}, 0.6f},  // dead ahead, a way off
        {2, {1.0f, 1.0f, -2.0f}, 0.6f},  // close, slightly right
        {3, {-4.0f, 1.0f, -4.0f}, 0.6f}, // off to the left
        {4, {0.0f, 1.0f, 6.0f}, 0.6f},   // behind
    };
}

void acquiresWhatThePlayerIsPointingAt()
{
    game::LockOnTuning tuning;
    const std::vector<game::LockCandidate> all = scene();
    const game::LockCandidate* got =
        game::acquireLockTarget(all, kEye, kForward, tuning);
    check(got != nullptr, "something in the cone is acquired");
    // Not the nearest body: the near one is off-axis, and grabbing it is the
    // classic "locked onto the rat at your feet" failure.
    check(got && got->id == 1, "the thing being pointed at wins over the near "
                               "one off to the side");
}

void refusesWhatIsBehindOrTooFar()
{
    game::LockOnTuning tuning;
    std::vector<game::LockCandidate> behind = {{4, {0.0f, 1.0f, 6.0f}, 0.6f}};
    check(!game::acquireLockTarget(behind, kEye, kForward, tuning),
          "nothing behind the player is acquired");

    std::vector<game::LockCandidate> far = {{5, {0.0f, 1.0f, -100.0f}, 0.6f}};
    check(!game::acquireLockTarget(far, kEye, kForward, tuning),
          "nor anything past the acquire range");

    // An empty cone locks onto nothing at all, rather than falling back to
    // whatever is nearest.
    std::vector<game::LockCandidate> none;
    check(!game::acquireLockTarget(none, kEye, kForward, tuning),
          "an empty field acquires nothing");
}

void switchingWalksLeftAndRightWithoutWrapping()
{
    game::LockOnTuning tuning;
    const std::vector<game::LockCandidate> all = scene();
    // From the one dead ahead, right is the one at +x, left the one at -x.
    const game::LockCandidate* right =
        game::switchLockTarget(all, 1, kEye, kForward, +1.0f, tuning);
    check(right && right->id == 2, "flicking right takes the target to the right");
    const game::LockCandidate* left =
        game::switchLockTarget(all, 1, kEye, kForward, -1.0f, tuning);
    check(left && left->id == 3, "flicking left takes the target to the left");
    // From the leftmost, there is nothing further left: stop rather than
    // teleport the camera across the arena.
    check(!game::switchLockTarget(all, 3, kEye, kForward, -1.0f, tuning),
          "running out on one side stops instead of wrapping");
}

void holdsSwitchesAndDrops()
{
    game::LockOnSystem lock;
    std::vector<game::LockCandidate> all = scene();

    game::LockOnSystem::Input press;
    press.togglePressed = true;
    lock.update(all, kEye, kForward, press, 1.0f / 60.0f);
    check(lock.locked() && lock.targetId() == 1, "the toggle acquires");
    check(lock.camera().active, "and the camera is told to frame it");

    // Held, with no input: it stays, and tracks the target as it moves.
    game::LockOnSystem::Input idle;
    all[0].point.x += 2.0f;
    lock.update(all, kEye, kForward, idle, 1.0f / 60.0f);
    glm::vec3 point{0.0f};
    check(lock.targetPoint(point) && point.x > 1.0f,
          "the lock follows its target");

    // A flick worth more than the threshold switches; a tremor does not.
    game::LockOnSystem::Input tremor;
    tremor.switchAxis = 4.0f;
    for (int i = 0; i < 20; ++i)
        lock.update(all, kEye, kForward, tremor, 1.0f / 60.0f);
    check(lock.targetId() == 1, "a slow drift never adds up to a switch");

    game::LockOnSystem::Input flick;
    flick.switchAxis = lock.tuning().switchThresholdPixels + 10.0f;
    lock.update(all, kEye, kForward, flick, 1.0f / 60.0f);
    check(lock.targetId() != 1, "a flick switches target");

    // The target dying removes it from the candidate list, which drops the lock
    // rather than leaving the camera on a corpse.
    all.clear();
    lock.update(all, kEye, kForward, idle, 1.0f / 60.0f);
    check(!lock.locked(), "a target that stops being a candidate drops the lock");
    check(!lock.camera().active, "and the camera stops framing it");
}

void occlusionHasAGracePeriod()
{
    game::LockOnSystem lock;
    game::LockOnTuning tuning;
    tuning.occlusionGrace = 0.5f;
    lock.setTuning(tuning);
    const std::vector<game::LockCandidate> all = scene();

    game::LockOnSystem::Input press;
    press.togglePressed = true;
    lock.update(all, kEye, kForward, press, 1.0f / 60.0f);
    check(lock.locked(), "acquired");

    const auto blind = [](glm::vec3, glm::vec3) { return false; };
    game::LockOnSystem::Input idle;
    // A few frames behind a pillar mid-swing must not cost the fight.
    for (int i = 0; i < 10; ++i)
        lock.update(all, kEye, kForward, idle, 1.0f / 60.0f, blind);
    check(lock.locked(), "a brief occlusion keeps the lock");
    // Long enough, and it goes.
    for (int i = 0; i < 60; ++i)
        lock.update(all, kEye, kForward, idle, 1.0f / 60.0f, blind);
    check(!lock.locked(), "staying out of sight eventually drops it");
}

void breakRangeIsWiderThanAcquireRange()
{
    game::LockOnSystem lock;
    game::LockOnTuning tuning;
    tuning.acquireRange = 10.0f;
    tuning.breakRange = 14.0f;
    lock.setTuning(tuning);

    std::vector<game::LockCandidate> all = {{7, {0.0f, 1.0f, -9.0f}, 0.6f}};
    game::LockOnSystem::Input press;
    press.togglePressed = true;
    lock.update(all, kEye, kForward, press, 1.0f / 60.0f);
    check(lock.locked(), "acquired just inside the acquire range");

    // Backing off past the acquire range does NOT drop it -- that gap is the
    // hysteresis that stops a lock flickering at the limit.
    game::LockOnSystem::Input idle;
    all[0].point.z = -12.0f;
    lock.update(all, kEye, kForward, idle, 1.0f / 60.0f);
    check(lock.locked(), "past acquire range but inside break range: held");
    all[0].point.z = -20.0f;
    lock.update(all, kEye, kForward, idle, 1.0f / 60.0f);
    check(!lock.locked(), "past break range: dropped");
}

} // namespace

int main()
{
    acquiresWhatThePlayerIsPointingAt();
    refusesWhatIsBehindOrTooFar();
    switchingWalksLeftAndRightWithoutWrapping();
    holdsSwitchesAndDrops();
    occlusionHasAGracePeriod();
    breakRangeIsWiderThanAcquireRange();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
