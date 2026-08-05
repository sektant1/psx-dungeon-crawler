// The camera rigs' *model*, without a renderer, a physics world or a window.
//
// This is the whole reason ThirdPersonCameraRig::solve() and boomLength() are
// separate from present(): the framing -- follow smoothing, pitch clamping,
// lock-on blending, the spring arm's in-fast/out-slow asymmetry -- is arithmetic
// that either holds or does not, and none of it needs a frame drawn to check.

#include <eng/camera/ScreenCameraRig.h>
#include <eng/camera/ThirdPersonCameraRig.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    if (!condition) {
        std::fprintf(stderr, "CameraRigTests: %s\n", what);
        ++failures;
    }
}

void checkNear(float value, float expected, float tolerance, const char* what)
{
    if (!(std::abs(value - expected) <= tolerance)) {
        std::fprintf(stderr, "CameraRigTests: %s (%.4f, expected %.4f)\n", what,
                     double(value), double(expected));
        ++failures;
    }
}

eng::CameraPose poseAt(glm::vec3 focus, float yaw = 0.0f, float pitch = 0.0f)
{
    eng::CameraPose pose;
    pose.focus = focus;
    pose.viewYaw = yaw;
    pose.viewPitch = pitch;
    pose.facingYaw = yaw;
    return pose;
}

// --- smoothing ------------------------------------------------------------
// The property the whole camera rests on: the same rate has to produce the same
// motion whatever the frame rate. A per-frame lerp does not, which is why
// smoothTowards exists at all.
void frameRateIndependence()
{
    const float rate = 8.0f;
    const float total = 0.5f;

    float coarse = 0.0f;
    for (int i = 0; i < 30; ++i)
        coarse = eng::smoothTowards(coarse, 1.0f, rate, total / 30.0f);
    float fine = 0.0f;
    for (int i = 0; i < 240; ++i)
        fine = eng::smoothTowards(fine, 1.0f, rate, total / 240.0f);

    checkNear(coarse, fine, 0.005f, "smoothing converges to the same place at "
                                    "30 and 240 steps");
    checkNear(coarse, 1.0f - std::exp(-rate * total), 0.005f,
              "smoothing follows 1 - e^(-rate*t)");
}

void angleWrapping()
{
    // Turning from just under +pi to just over -pi is a short hop across the
    // wrap, not a trip the long way round the compass.
    const float from = 3.0f;
    const float to = -3.0f;
    // The gap the short way is 2*pi - 6 = 0.283 rad, so a 0.1 rad step must
    // move *up* past pi rather than 0.1 rad back down towards zero.
    const float stepped = eng::turnTowards(from, to, 0.1f);
    check(stepped > from, "a turn across the wrap goes the short way");
    checkNear(eng::wrapAngle(stepped - from), 0.1f, 1e-4f,
              "and moves exactly the rate it was given");
    // A step bigger than the remaining gap arrives rather than overshooting.
    checkNear(eng::turnTowards(from, to, 0.5f), to, 1e-4f,
              "a step larger than what is left arrives exactly");
    // yawOfDirection agrees with the renderer's convention: yaw 0 looks down -Z.
    checkNear(eng::yawOfDirection({0.0f, 0.0f, -1.0f}), 0.0f, 1e-4f,
              "yaw 0 looks down -Z");
    checkNear(eng::yawOfDirection({-1.0f, 0.0f, 0.0f}), 1.5707963f, 1e-4f,
              "+90 degrees of yaw looks down -X");
}

// --- third person ---------------------------------------------------------
void followLagsVerticallyMoreThanHorizontally()
{
    eng::ThirdPersonCameraRig rig;
    eng::ecs::ThirdPersonCamera tuning;
    tuning.followRate = 18.0f;
    tuning.followRateVertical = 7.0f;
    rig.setTuning(tuning);

    rig.snapTo(poseAt({0.0f, 0.0f, 0.0f}));
    // One step of the character moving diagonally by the same amount on both
    // axes. The camera should have covered more of the horizontal one.
    const eng::ThirdPersonCameraRig::Solution s =
        rig.solve(poseAt({1.0f, 1.0f, 0.0f}), 1.0f / 60.0f);
    const float horizontal = s.pivot.x - tuning.shoulderOffset;
    const float vertical = s.pivot.y - tuning.pivotHeight;
    check(horizontal > vertical,
          "the camera follows horizontally faster than vertically -- stairs");
    check(vertical > 0.0f, "but it does follow vertically");
}

void pitchIsClampedToTheTuning()
{
    eng::ThirdPersonCameraRig rig;
    eng::ecs::ThirdPersonCamera tuning;
    tuning.pitchMinDegrees = -50.0f;
    tuning.pitchMaxDegrees = 20.0f;
    rig.setTuning(tuning);
    rig.snapTo(poseAt({0.0f, 0.0f, 0.0f}));

    const auto down = rig.solve(poseAt({0, 0, 0}, 0.0f, glm::radians(-89.0f)),
                                1.0f / 60.0f);
    checkNear(down.pitch, glm::radians(-50.0f), 1e-4f,
              "looking hard down clamps at pitchMinDegrees");
    const auto up = rig.solve(poseAt({0, 0, 0}, 0.0f, glm::radians(89.0f)),
                              1.0f / 60.0f);
    checkNear(up.pitch, glm::radians(20.0f), 1e-4f,
              "looking hard up clamps at pitchMaxDegrees");
    // And the controller is told the same range, so it never banks an angle
    // outside it that the player would then have to unwind.
    checkNear(rig.pitchLimitsRadians().x, glm::radians(-50.0f), 1e-4f,
              "the rig publishes its lower limit");
    checkNear(rig.pitchLimitsRadians().y, glm::radians(20.0f), 1e-4f,
              "the rig publishes its upper limit");
}

void lockOnSwingsOntoTheTargetAndFramesBetween()
{
    eng::ThirdPersonCameraRig rig;
    eng::ecs::ThirdPersonCamera tuning;
    tuning.shoulderOffset = 0.0f; // isolate the framing from the shoulder bias
    tuning.lockBlendRate = 12.0f;
    rig.setTuning(tuning);
    rig.snapTo(poseAt({0.0f, 0.0f, 0.0f}));

    // A target due -X of the player, which is yaw = +90 degrees.
    eng::CameraLockOn lock;
    lock.active = true;
    lock.point = {-6.0f, 1.0f, 0.0f};
    rig.setLockOn(lock);

    eng::ThirdPersonCameraRig::Solution s;
    for (int i = 0; i < 120; ++i)
        s = rig.solve(poseAt({0.0f, 0.0f, 0.0f}), 1.0f / 60.0f);

    check(s.locked, "the solution reports the lock");
    checkNear(s.yaw, 1.5707963f, 0.02f, "the camera ends up facing the target");
    // The framing point is between the player and the target, not on either.
    check(s.look.x < -0.2f, "the framing point leans towards the target");
    check(s.look.x > -6.0f, "but does not sit on it");
    // And the view angles are handed back, so releasing the lock is continuous
    // and movement stays camera-relative under it.
    float yaw = 0.0f;
    float pitch = 0.0f;
    check(rig.viewOverride(yaw, pitch), "a held lock owns the view angles");
    checkNear(yaw, s.yaw, 1e-4f, "and hands back the angles it solved");

    // Distant targets pull the boom out, so a fight across a room does not
    // shrink both fighters into the same few pixels.
    check(s.distance > tuning.distance,
          "the boom lengthens with the distance to the target");
}

void unlockedTheMouseOwnsTheOrbitOutright()
{
    eng::ThirdPersonCameraRig rig;
    rig.snapTo(poseAt({0.0f, 0.0f, 0.0f}));
    const auto s = rig.solve(poseAt({0, 0, 0}, 1.0f, 0.2f), 1.0f / 60.0f);
    checkNear(s.yaw, 1.0f, 1e-5f, "no smoothing between the mouse and the yaw");
    checkNear(s.pitch, 0.2f, 1e-5f, "nor the pitch -- that would be input lag");
    float yaw = 0.0f;
    float pitch = 0.0f;
    check(!rig.viewOverride(yaw, pitch),
          "and with no lock the rig claims nothing");
}

void springArmComesInFastAndGoesOutSlow()
{
    eng::ThirdPersonCameraRig rig;
    eng::ecs::ThirdPersonCamera tuning;
    tuning.distance = 4.0f;
    tuning.pushOutSpeed = 2.0f; // metres per second
    tuning.minDistance = 0.5f;
    rig.setTuning(tuning);

    // A wall at 1 m takes the boom there in one step, however long the step is.
    checkNear(rig.boomLength(4.0f, 4.0f, 1.0f, 1.0f / 60.0f), 1.0f, 1e-5f,
              "the boom comes in on the frame the wall appears");
    // Clear again, it may only extend by pushOutSpeed * dt.
    checkNear(rig.boomLength(1.0f, 4.0f, 4.0f, 0.5f), 2.0f, 1e-5f,
              "and goes back out at push-out speed, not instantly");
    // A pinch never drives it inside the character's head.
    checkNear(rig.boomLength(4.0f, 4.0f, 0.0f, 1.0f / 60.0f), 0.5f, 1e-5f,
              "and never closer than minDistance");
}

// --- screen ---------------------------------------------------------------
void screenFitsThePage()
{
    eng::ecs::ScreenCamera page;
    page.pageWidth = 400.0f;
    page.pageHeight = 200.0f;
    page.fovDegrees = 60.0f;

    // Fit::Height: the distance is exactly the one that makes the page's height
    // subtend the vertical field, whatever the window's aspect is.
    const float expected = 100.0f / std::tan(glm::radians(30.0f));
    checkNear(eng::ScreenCameraRig::fitDistance(page, 16.0f / 9.0f), expected,
              0.01f, "height fit puts the page height across the view");
    checkNear(eng::ScreenCameraRig::fitDistance(page, 1.0f), expected, 0.01f,
              "and does not depend on the aspect");

    // Fit::Contain: a window narrower than the page pulls the camera back.
    page.fit = eng::ecs::ScreenCamera::Contain;
    check(eng::ScreenCameraRig::fitDistance(page, 1.0f) > expected,
          "contain pulls back when the window is narrower than the page");
    checkNear(eng::ScreenCameraRig::fitDistance(page, 4.0f), expected, 0.01f,
              "and matches the height fit once the window is wide enough");
}

void screenMapsPixelsToThePage()
{
    eng::ScreenCameraRig rig;
    eng::ecs::ScreenCamera page;
    page.pageWidth = 400.0f;
    page.pageHeight = 200.0f;
    page.origin = eng::ecs::ScreenCamera::TopLeft;
    page.layerSpacing = 0.5f;
    rig.setPage(page);

    const glm::vec3 corner = rig.pagePoint({0.0f, 0.0f});
    checkNear(corner.x, -200.0f, 1e-4f, "top-left x is the page's left edge");
    checkNear(corner.y, 100.0f, 1e-4f, "top-left y is the page's top edge");
    const glm::vec3 layered = rig.pagePoint({0.0f, 0.0f}, 2);
    checkNear(layered.z, -1.0f, 1e-4f, "layers step by layerSpacing");

    page.origin = eng::ecs::ScreenCamera::Centre;
    rig.setPage(page);
    const glm::vec3 centred = rig.pagePoint({10.0f, 20.0f});
    checkNear(centred.x, 10.0f, 1e-4f, "centre origin is world-like: x is x");
    checkNear(centred.y, 20.0f, 1e-4f, "and y is up");
}

} // namespace

int main()
{
    frameRateIndependence();
    angleWrapping();
    followLagsVerticallyMoreThanHorizontally();
    pitchIsClampedToTheTuning();
    lockOnSwingsOntoTheTargetAndFramesBetween();
    unlockedTheMouseOwnsTheOrbitOutright();
    springArmComesInFastAndGoesOutSlow();
    screenFitsThePage();
    screenMapsPixelsToThePage();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
