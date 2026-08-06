#include <eng/controllers/FpsController.h>

#include <glm/gtc/constants.hpp>

#include <cstdlib>
#include <cmath>

// Drives the controller with no physics backend (AABB fallback) to exercise
// locomotion + the sustained-sprint stamina model.
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
    if (player.position().z >= -1.0f)
        return EXIT_FAILURE;

    // Sprint engages and drains stamina.
    command.sprint = true;
    const float before = player.sprintStamina();
    for (int i = 0; i < 30; ++i)
        player.simulate(command, 1.0f / 60.0f);
    if (!player.sprinting() || player.sprintStamina() >= before)
        return EXIT_FAILURE;

    // Held long enough, sprint eventually exhausts (bounded ~15 s search).
    bool exhausted = false;
    for (int i = 0; i < 900 && !exhausted; ++i) {
        player.simulate(command, 1.0f / 60.0f);
        if (!player.sprinting())
            exhausted = true;
    }
    if (!exhausted)
        return EXIT_FAILURE;

    // Hysteresis: immediately after exhaustion, a still-held sprint must NOT
    // re-engage while stamina is climbing back through the recovery band
    // (this is the anti-flap guarantee).
    for (int i = 0; i < 12; ++i) {
        player.simulate(command, 1.0f / 60.0f);
        if (player.sprinting())
            return EXIT_FAILURE;
    }

    // Keep holding: once stamina clears the recovery threshold, sprint
    // auto-resumes without needing to release the key.
    bool resumed = false;
    for (int i = 0; i < 240 && !resumed; ++i) {
        player.simulate(command, 1.0f / 60.0f);
        if (player.sprinting())
            resumed = true;
    }
    if (!resumed)
        return EXIT_FAILURE;

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
    if (!player.beginDash({0.0f, -1.0f}))
        return EXIT_FAILURE;
    eng::FpsController::Command dashCommand;
    for (int i = 0; i < 9; ++i)
        player.simulate(dashCommand, 1.0f / 60.0f);
    if (player.dashCameraDrop() < 0.30f || player.crouched() ||
        std::abs(std::abs(player.dashRollRadians()) - glm::pi<float>()) > 0.2f)
        return EXIT_FAILURE;
    for (int i = 0; i < 10; ++i)
        player.simulate(dashCommand, 1.0f / 60.0f);
    if (player.dashing() || player.dashCameraDrop() > 0.001f ||
        std::abs(std::abs(player.dashRollRadians()) - glm::two_pi<float>()) >
            0.2f)
        return EXIT_FAILURE;

    // --- movement tuning ----------------------------------------------------
    // A tuning is applied whole or not at all: a controller running the new
    // acceleration against the old friction is a feel nobody authored.
    {
        eng::MovementTuning tuning;
        tuning.moveSpeed = 8.5f;
        tuning.groundAcceleration = 90.0f;
        if (!eng::validMovementTuning(tuning))
            return EXIT_FAILURE;

        eng::FpsController tuned;
        tuned.reset(glm::vec3(0.0f), 3.0f, 0.002f, glm::vec3(-1000.0f),
                    glm::vec3(1000.0f));
        if (!tuned.setMovementTuning(tuning))
            return EXIT_FAILURE;
        // speed() is an alias for the tuning's move speed, not a second copy.
        if (std::abs(tuned.speed() - 8.5f) > 0.001f)
            return EXIT_FAILURE;

        // 90 m/s^2 covers 0 -> 8.5 m/s in ~0.09 s, so a tenth of a second of
        // input must already be at full speed. This is the "no ramp you can
        // feel" property the whole tuning exists to deliver.
        eng::FpsController::Command run;
        run.move.y = 1.0f;
        for (int i = 0; i < 6; ++i)
            tuned.simulate(run, 1.0f / 60.0f);
        if (tuned.horizontalSpeed() < 8.0f)
            return EXIT_FAILURE;

        // And releasing input stops it about as fast.
        for (int i = 0; i < 12; ++i)
            tuned.simulate({}, 1.0f / 60.0f);
        if (tuned.horizontalSpeed() > 0.5f)
            return EXIT_FAILURE;

        // A rejected tuning must leave the live one untouched rather than
        // half-applied. Non-finite is the case a bad TOML edit produces.
        eng::MovementTuning broken = tuning;
        broken.groundFriction = std::nanf("");
        if (eng::validMovementTuning(broken) ||
            tuned.setMovementTuning(broken))
            return EXIT_FAILURE;
        if (std::abs(tuned.movementTuning().groundAcceleration - 90.0f) > 0.001f)
            return EXIT_FAILURE;

        // Zero or negative rates are rejected for the same reason: a zero
        // acceleration is a player who cannot move, and it reads as a hang.
        broken = tuning;
        broken.jumpVelocity = 0.0f;
        if (eng::validMovementTuning(broken))
            return EXIT_FAILURE;

        // Jump velocity is the arc: a taller jump must actually leave the
        // ground faster, which is what makes it tunable rather than decorative.
        eng::MovementTuning high = tuning;
        high.jumpVelocity = 7.5f;
        eng::FpsController jumper;
        jumper.reset(glm::vec3(0.0f), 8.5f, 0.002f, glm::vec3(-1000.0f),
                     glm::vec3(1000.0f));
        if (!jumper.setMovementTuning(high))
            return EXIT_FAILURE;
        eng::FpsController::Command jump;
        jump.jumpPressed = true;
        jumper.simulate(jump, 1.0f / 60.0f);
        if (jumper.verticalSpeed() < 7.0f)
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
