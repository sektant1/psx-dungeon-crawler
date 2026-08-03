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

    return EXIT_SUCCESS;
}
