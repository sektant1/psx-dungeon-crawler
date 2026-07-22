#include "FpsController.h"

#include <cstdlib>

// Drives the controller with no physics backend (AABB fallback) to exercise
// locomotion + the sustained-sprint stamina model.
int main()
{
    FpsController player;
    player.reset(glm::vec3(0.0f), 3.0f, 0.002f,
                 glm::vec3(-100.0f), glm::vec3(100.0f));
    FpsController::Command command;
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

    return EXIT_SUCCESS;
}
