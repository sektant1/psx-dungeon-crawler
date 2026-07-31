// Screen ray + AABB, the two bits of maths a click in the viewport depends on.
// Recovered with the picker from the deleted editor, plus the work-plane
// intersection the placement tool needs.

#include "Picker.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorPickerTests: " << message << '\n';
        std::exit(1);
    }
}

static bool nearly(float a, float b, float tolerance = 1e-3f)
{
    return std::fabs(a - b) < tolerance;
}

int main()
{
    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f); // looking down -Z
    const float fov = glm::radians(65.0f);

    // --- screen ray ---------------------------------------------------------
    {
        const Ray centre = screenRay({0.0f, 0.0f}, {0.0f, 0.0f, 10.0f}, identity,
                                     fov, 16.0f / 9.0f);
        require(nearly(centre.dir.x, 0.0f) && nearly(centre.dir.y, 0.0f),
                "a ray through the middle of the screen has no lateral tilt");
        require(centre.dir.z < 0.0f, "and points where the camera looks");
        require(nearly(glm::length(centre.dir), 1.0f), "the direction is unit");
        require(centre.origin.z == 10.0f, "it starts at the eye");

        const Ray right = screenRay({1.0f, 0.0f}, {}, identity, fov, 1.0f);
        require(right.dir.x > 0.0f, "the right edge of the screen tilts right");
        const Ray up = screenRay({0.0f, 1.0f}, {}, identity, fov, 1.0f);
        require(up.dir.y > 0.0f, "and the top edge tilts up");

        // Aspect widens the horizontal spread, not the vertical one.
        const Ray wide = screenRay({1.0f, 0.0f}, {}, identity, fov, 2.0f);
        require(wide.dir.x > right.dir.x,
                "a wider viewport spreads the horizontal fov");
    }

    // --- viewport conversion ------------------------------------------------
    {
        const glm::vec2 origin{120.0f, 80.0f};
        const glm::vec2 size{800.0f, 600.0f};
        const Ray centre = viewportRay(origin + size * 0.5f, origin, size,
                                       {0.0f, 0.0f, 10.0f}, identity, fov);
        require(nearly(centre.dir.x, 0.0f) && nearly(centre.dir.y, 0.0f) &&
                    centre.dir.z < 0.0f,
                "viewport centre uses its offset and points straight ahead");

        const Ray topRight = viewportRay(origin + glm::vec2(size.x, 0.0f),
                                         origin, size, {}, identity, fov);
        require(topRight.dir.x > 0.0f && topRight.dir.y > 0.0f,
                "viewport pixels map right and invert screen Y");

        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 10.0f),
                                           glm::vec3(0.0f),
                                           glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection =
            glm::perspective(fov, size.x / size.y, 0.05f, 100.0f);
        glm::vec2 projected;
        require(projectToViewport(glm::vec3(0.0f), projection * view,
                                  origin, size, projected),
                "point in front of camera projects into viewport");
        require(nearly(projected.x, origin.x + size.x * 0.5f) &&
                    nearly(projected.y, origin.y + size.y * 0.5f),
                "world centre projects to offset viewport centre");
        require(!projectToViewport(glm::vec3(0.0f, 0.0f, 20.0f),
                                   projection * view, origin, size, projected),
                "point behind camera is rejected");
    }

    // --- ray vs box ---------------------------------------------------------
    {
        Ray ray;
        ray.origin = {0.0f, 0.0f, 10.0f};
        ray.dir = {0.0f, 0.0f, -1.0f};
        float t = 0.0f;
        require(rayAabb(ray, {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, t),
                "a ray fired at a box hits it");
        require(nearly(t, 9.0f), "at the near face");

        require(!rayAabb(ray, {5.0f, 5.0f, -1.0f}, {6.0f, 6.0f, 1.0f}, t),
                "and misses a box beside it");

        // Behind the camera: tmin stays clamped at 0, so a box the ray points
        // away from must not report a hit.
        ray.dir = {0.0f, 0.0f, 1.0f};
        require(!rayAabb(ray, {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, t),
                "a box behind the ray is not a hit");

        // A ray parallel to a slab it is outside of: the degenerate case that
        // divides by zero if it is not special-cased.
        ray.origin = {0.0f, 50.0f, 10.0f};
        ray.dir = {0.0f, 0.0f, -1.0f};
        require(!rayAabb(ray, {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, t),
                "parallel and outside is a miss, not a divide by zero");
    }

    // --- work plane ---------------------------------------------------------
    {
        Ray ray;
        ray.origin = {0.0f, 10.0f, 0.0f};
        ray.dir = glm::normalize(glm::vec3{0.0f, -1.0f, -1.0f});
        glm::vec3 hit{};
        require(rayPlaneY(ray, 0.0f, hit), "a downward ray meets the floor");
        require(nearly(hit.y, 0.0f), "at the plane height");
        require(nearly(hit.z, -10.0f), "and the expected distance out");

        require(rayPlaneY(ray, 4.0f, hit), "a raised work plane too");
        require(nearly(hit.y, 4.0f) && nearly(hit.z, -6.0f),
                "which is nearer than the floor");

        ray.dir = {0.0f, 1.0f, 0.0f};
        require(!rayPlaneY(ray, 0.0f, hit),
                "a ray pointing away from the plane does not hit it");
        ray.dir = {1.0f, 0.0f, 0.0f};
        require(!rayPlaneY(ray, 0.0f, hit), "nor one parallel to it");
    }

    std::cout << "EditorPickerTests: ok\n";
    return 0;
}
