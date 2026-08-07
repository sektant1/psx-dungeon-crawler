#include <eng/controllers/FpsController.h>

#include <glm/gtc/constants.hpp>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

// Drives the controller with no physics backend (AABB fallback) to exercise
// locomotion + the sustained-sprint stamina model.
//
// Every check reports what it was. This file used to be a column of bare
// `return EXIT_FAILURE`, so a failing run printed nothing at all and the only
// way to learn which of two dozen properties had broken was to bisect it by
// hand -- which is why it sat red.

namespace {

int gFailures = 0;

void require(bool condition, const std::string& what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++gFailures;
    }
}

} // namespace

int main()
{
    eng::FpsController player;
    player.reset(glm::vec3(0.0f), 3.0f, 0.002f,
                 glm::vec3(-100.0f), glm::vec3(100.0f));
    eng::FpsController::Command command;
    command.move.y = 1.0f;

    // Walks forward under normal input.
    for (int i = 0; i < 60; ++i)
        player.simulate(command, 1.0f / 60.0f);
    require(player.position().z < -1.0f,
            "a second of forward input walks forward (z=" +
                std::to_string(player.position().z) + ", want < -1)");

    // Sprint engages and drains stamina.
    command.sprint = true;
    const float before = player.sprintStamina();
    for (int i = 0; i < 30; ++i)
        player.simulate(command, 1.0f / 60.0f);
    require(player.sprinting(), "holding sprint engages it");
    require(player.sprintStamina() < before,
            "sprinting drains stamina (" + std::to_string(before) + " -> " +
                std::to_string(player.sprintStamina()) + ")");

    // Held long enough, sprint eventually exhausts (bounded ~15 s search).
    bool exhausted = false;
    for (int i = 0; i < 900 && !exhausted; ++i) {
        player.simulate(command, 1.0f / 60.0f);
        if (!player.sprinting())
            exhausted = true;
    }
    require(exhausted, "a sprint held for 15 s exhausts");

    // Hysteresis: immediately after exhaustion, a still-held sprint must NOT
    // re-engage while stamina is climbing back through the recovery band
    // (this is the anti-flap guarantee).
    bool flapped = false;
    for (int i = 0; i < 12; ++i) {
        player.simulate(command, 1.0f / 60.0f);
        flapped = flapped || player.sprinting();
    }
    require(!flapped,
            "an exhausted sprint does not re-engage the moment stamina ticks "
            "up (the anti-flap band)");

    // Keep holding: once stamina clears the recovery threshold, sprint
    // auto-resumes without needing to release the key.
    bool resumed = false;
    for (int i = 0; i < 240 && !resumed; ++i) {
        player.simulate(command, 1.0f / 60.0f);
        if (player.sprinting())
            resumed = true;
    }
    require(resumed, "sprint auto-resumes once stamina recovers, key still held");

    // Dash presentation follows one complete roll plus crouch-shaped Y arc,
    // without changing collision crouch state.
    player.reset(glm::vec3(0.0f), 3.0f, 0.002f,
                 glm::vec3(-100.0f), glm::vec3(100.0f));
    eng::FpsController::DashTuning dash;
    dash.duration = 0.30f;
    dash.cooldown = 0.10f;
    dash.cameraDrop = 0.34f;
    dash.cameraRollDegrees = 360.0f;
    player.setDashTuning(dash);
    require(player.beginDash({0.0f, -1.0f}), "a dash starts from rest");
    eng::FpsController::Command dashCommand;
    for (int i = 0; i < 9; ++i)
        player.simulate(dashCommand, 1.0f / 60.0f);
    require(player.dashCameraDrop() >= 0.30f,
            "mid-dash the camera has dropped (" +
                std::to_string(player.dashCameraDrop()) + ", want >= 0.30)");
    require(!player.crouched(),
            "the dash's camera drop is presentation only and does not crouch "
            "the collision capsule");
    require(std::abs(std::abs(player.dashRollRadians()) - glm::pi<float>()) <= 0.2f,
            "mid-dash the roll is about half a turn (" +
                std::to_string(player.dashRollRadians()) + " rad)");
    for (int i = 0; i < 10; ++i)
        player.simulate(dashCommand, 1.0f / 60.0f);
    require(!player.dashing(), "the dash ends after its duration");
    require(player.dashCameraDrop() <= 0.001f,
            "the camera returns to level after the dash (" +
                std::to_string(player.dashCameraDrop()) + ")");
    require(std::abs(std::abs(player.dashRollRadians()) -
                     glm::two_pi<float>()) <= 0.2f,
            "the dash completes exactly one full roll (" +
                std::to_string(player.dashRollRadians()) + " rad)");

    // --- movement tuning ----------------------------------------------------
    // A tuning is applied whole or not at all: a controller running the new
    // acceleration against the old friction is a feel nobody authored.
    {
        eng::MovementTuning tuning;
        tuning.moveSpeed = 8.5f;
        tuning.groundAcceleration = 90.0f;
        // Friction has to be set too, and this test used to leave it at the
        // 34 m/s^2 default while demanding a stop from 8.5 m/s inside 0.2 s.
        // Deceleration here is a LINEAR approach, not exponential: 34 * 0.2 is
        // 6.8 m/s of the 8.5, leaving 1.7 -- so the check could not pass at any
        // point, whatever the controller did. Stating the friction is also what
        // the assertion below actually means by "about as fast".
        tuning.groundFriction = 90.0f;
        require(eng::validMovementTuning(tuning),
                "a plain boomer-shooter tuning validates");

        eng::FpsController tuned;
        tuned.reset(glm::vec3(0.0f), 3.0f, 0.002f, glm::vec3(-1000.0f),
                    glm::vec3(1000.0f));
        require(tuned.setMovementTuning(tuning), "a valid tuning applies");
        // speed() is an alias for the tuning's move speed, not a second copy.
        require(std::abs(tuned.speed() - 8.5f) <= 0.001f,
                "speed() reads the tuning rather than a stale copy (" +
                    std::to_string(tuned.speed()) + ")");

        // 90 m/s^2 covers 0 -> 8.5 m/s in ~0.09 s, so a tenth of a second of
        // input must already be at full speed. This is the "no ramp you can
        // feel" property the whole tuning exists to deliver.
        eng::FpsController::Command run;
        run.move.y = 1.0f;
        for (int i = 0; i < 6; ++i)
            tuned.simulate(run, 1.0f / 60.0f);
        require(tuned.horizontalSpeed() >= 8.0f,
                "0.1 s of input reaches full speed -- no ramp you can feel (" +
                    std::to_string(tuned.horizontalSpeed()) + " m/s, want >= 8)");

        // And releasing input stops it about as fast.
        for (int i = 0; i < 12; ++i)
            tuned.simulate({}, 1.0f / 60.0f);
        require(tuned.horizontalSpeed() <= 0.5f,
                "releasing input stops about as fast (" +
                    std::to_string(tuned.horizontalSpeed()) + " m/s)");

        // A rejected tuning must leave the live one untouched rather than
        // half-applied. Non-finite is the case a bad TOML edit produces.
        eng::MovementTuning broken = tuning;
        broken.groundFriction = std::nanf("");
        require(!eng::validMovementTuning(broken),
                "a non-finite friction is rejected");
        require(!tuned.setMovementTuning(broken),
                "applying a rejected tuning fails");
        require(std::abs(tuned.movementTuning().groundAcceleration - 90.0f) <=
                    0.001f,
                "a rejected tuning leaves the live one whole, not half-applied");

        // Zero or negative rates are rejected for the same reason: a zero
        // acceleration is a player who cannot move, and it reads as a hang.
        broken = tuning;
        broken.jumpVelocity = 0.0f;
        require(!eng::validMovementTuning(broken),
                "a zero jump velocity is rejected");

        // Jump velocity is the arc: a taller jump must actually leave the
        // ground faster, which is what makes it tunable rather than decorative.
        eng::MovementTuning high = tuning;
        high.jumpVelocity = 7.5f;
        eng::FpsController jumper;
        jumper.reset(glm::vec3(0.0f), 8.5f, 0.002f, glm::vec3(-1000.0f),
                     glm::vec3(1000.0f));
        require(jumper.setMovementTuning(high), "the taller tuning applies");
        eng::FpsController::Command jump;
        jump.jumpPressed = true;
        jumper.simulate(jump, 1.0f / 60.0f);
        require(jumper.verticalSpeed() >= 7.0f,
                "a 7.5 m/s jump tuning actually leaves the ground at ~7.5 (" +
                    std::to_string(jumper.verticalSpeed()) + " m/s)");
    }

    if (gFailures == 0)
        std::printf("fps_controller: all checks passed\n");
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
