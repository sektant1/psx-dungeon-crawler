#include "Picker.h"

#include <glm/gtc/epsilon.hpp>

#include <cstdlib>
#include <iostream>

using namespace editor;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "PickerTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    Ray centre = screenRay({0.0f, 0.0f}, glm::vec3(0, 0, 0),
                           glm::quat(1, 0, 0, 0), glm::radians(60.0f), 1.0f);
    require(glm::all(glm::epsilonEqual(centre.origin, glm::vec3(0), 1e-5f)),
            "ray origin is the camera position");
    require(centre.dir.z < -0.9f, "centre ray points down -Z");

    float t = 0.0f;
    require(rayAabb(centre, glm::vec3(-1, -1, -6), glm::vec3(1, 1, -4), t),
            "centre ray hits a box straight ahead");
    require(t > 3.5f && t < 4.5f, "hit distance is the near face (~4)");

    float t2 = 0.0f;
    require(!rayAabb(centre, glm::vec3(100, -1, -6), glm::vec3(102, 1, -4), t2),
            "centre ray misses an off-axis box");

    std::cout << "PickerTests OK\n";
    return 0;
}
