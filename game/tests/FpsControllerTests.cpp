#include "FpsController.h"

#include <cstdlib>

int main()
{
    FpsController player;
    player.reset(glm::vec3(0.0f), 3.0f, 0.002f,
                 glm::vec3(-100.0f), glm::vec3(100.0f));
    FpsController::Command command;
    command.move.y = 1.0f;
    for (int i = 0; i < 60; ++i)
        player.simulate(command, 1.0f / 60.0f);
    if (player.position().z >= -1.0f)
        return EXIT_FAILURE;

    command.sprint = true;
    const float before = player.sprintStamina();
    for (int i = 0; i < 30; ++i)
        player.simulate(command, 1.0f / 60.0f);
    if (!player.sprinting() || player.sprintStamina() >= before)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
