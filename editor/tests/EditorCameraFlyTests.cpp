#include <editor/viewport/EditorCamera.h>

#include <glm/gtc/epsilon.hpp>

#include <cstdlib>
#include <iostream>

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "EditorCameraFlyTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    EditorCamera cam;
    cam.setFlyPosition(glm::vec3(0, 0, 0));
    cam.setYawPitch(0.0f, 0.0f);

    cam.moveLocal(glm::vec3(0, 0, -2.0f));
    const glm::vec3 p = cam.flyEye();
    require(glm::all(glm::epsilonEqual(p, glm::vec3(0, 0, -2.0f), 1e-4f)),
            "forward move translates along view forward");

    cam.setYawPitch(glm::radians(90.0f), 0.0f);
    const glm::vec3 before = cam.flyEye();
    cam.moveLocal(glm::vec3(1.0f, 0, 0));
    const glm::vec3 after = cam.flyEye();
    require(glm::abs((after - before).z) > 0.5f,
            "right move after 90deg yaw translates along world Z");

    cam.setYawPitch(0.0f, glm::radians(89.0f));
    cam.addYawPitch(0.0f, glm::radians(20.0f));
    require(cam.pitch() < glm::radians(90.0f), "pitch clamps below +90 deg");

    std::cout << "EditorCameraFlyTests OK\n";
    return 0;
}
