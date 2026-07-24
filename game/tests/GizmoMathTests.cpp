#include "Gizmo.h"
#include "Picker.h"

#include <glm/gtc/epsilon.hpp>

#include <cstdlib>
#include <iostream>

using namespace editor;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "GizmoMathTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    Ray down; down.origin = glm::vec3(3, 5, 0); down.dir = glm::vec3(0, -1, 0);
    float axisT = 0.0f;
    require(closestPointOnAxis(glm::vec3(0), glm::vec3(1, 0, 0), down, axisT),
            "ray not parallel to axis yields a solution");
    require(std::abs(axisT - 3.0f) < 1e-3f, "closest axis param is ~3");

    Ray d2; d2.origin = glm::vec3(2, 4, -1); d2.dir = glm::vec3(0, -1, 0);
    glm::vec3 hit;
    require(rayPlane(d2, glm::vec3(0), glm::vec3(0, 1, 0), hit),
            "ray hits the ground plane");
    require(glm::all(glm::epsilonEqual(hit, glm::vec3(2, 0, -1), 1e-4f)),
            "plane hit point is directly below the origin");

    require(std::abs(snap(1.24f, 0.25f) - 1.25f) < 1e-5f, "snap rounds to step");
    require(std::abs(snap(-0.10f, 0.25f) - 0.0f) < 1e-5f, "snap rounds toward zero");

    // signedAngleAround: +X to +Z about +Y axis is +90deg (right-hand rule:
    // rotating +X by +90 about +Y gives -Z, so +X->+Z is -90).
    const float a = signedAngleAround(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1),
                                      glm::vec3(0, 1, 0));
    require(std::abs(a + glm::radians(90.0f)) < 1e-4f,
            "+X to +Z about +Y is -90 degrees");
    const float b = signedAngleAround(glm::vec3(1, 0, 0), glm::vec3(0, 0, -1),
                                      glm::vec3(0, 1, 0));
    require(std::abs(b - glm::radians(90.0f)) < 1e-4f,
            "+X to -Z about +Y is +90 degrees");
    // Components parallel to the axis are ignored.
    const float c = signedAngleAround(glm::vec3(1, 5, 0), glm::vec3(0, -3, 1),
                                      glm::vec3(0, 1, 0));
    require(std::abs(c + glm::radians(90.0f)) < 1e-4f,
            "axis-parallel components do not affect the angle");

    std::cout << "GizmoMathTests OK\n";
    return 0;
}
