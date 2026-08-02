#include <editor/viewport/EditorCamera.h>

#include <glm/trigonometric.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "EditorCameraEyeTests: " << m << '\n'; std::exit(1); }
}

// Reports the numbers on failure, because "eye height wrong" without the actual
// value tells you nothing about whether the bug is the constant or the maths.
static void requireNear(float actual, float expected, float eps, const char* m)
{
    if (std::abs(actual - expected) > eps) {
        std::cerr << "EditorCameraEyeTests: " << m << " (actual " << actual
                  << ", expected " << expected << ")\n";
        std::exit(1);
    }
}

int main()
{
    // The eye must sit exactly one player height above the requested floor
    // point, since that is the whole claim the preview makes.
    {
        EditorCamera cam;
        cam.enterWalk(glm::vec3(3.0f, 0.5f, -7.0f), 0.0f);
        const glm::vec3 eye = cam.walkEye();
        requireNear(eye.x, 3.0f, 1e-5f, "walk eye keeps the floor X");
        requireNear(eye.y, 0.5f + 1.7f, 1e-5f, "walk eye is 1.7m above the floor");
        requireNear(eye.z, -7.0f, 1e-5f, "walk eye keeps the floor Z");
        requireNear(EditorCamera::kPlayerEyeHeight, 1.7f, 1e-6f,
                    "kPlayerEyeHeight must stay in sync with game/src/main.cpp");
    }

    // Pitch clamps at both extremes so the preview cannot look past vertical.
    {
        EditorCamera cam;
        cam.enterWalk(glm::vec3(0.0f), 0.0f);
        cam.walkLook(0.0f, glm::radians(400.0f));
        requireNear(glm::degrees(cam.walkPitch()), 85.0f, 1e-3f,
                    "pitch clamps at +85 deg");
        cam.walkLook(0.0f, glm::radians(-800.0f));
        requireNear(glm::degrees(cam.walkPitch()), -85.0f, 1e-3f,
                    "pitch clamps at -85 deg");
    }

    // Yaw wraps into (-pi, pi] instead of accumulating without bound, and the
    // wrap must not change where the camera actually points.
    {
        EditorCamera cam;
        cam.enterWalk(glm::vec3(0.0f), 350.0f);
        cam.walkLook(glm::radians(30.0f), 0.0f);
        const float deg = glm::degrees(cam.walkYaw());
        require(deg > -180.0f && deg <= 180.0f,
                "yaw stays wrapped into (-180, 180]");
        requireNear(deg, 20.0f, 1e-2f, "350 deg + 30 deg wraps to 20 deg");
    }

    // Movement is a preview slide on the floor plane: the height never moves,
    // whatever the pitch, and forward means forward.
    {
        EditorCamera cam;
        cam.enterWalk(glm::vec3(0.0f), 0.0f);
        cam.walkLook(0.0f, glm::radians(85.0f)); // staring at the ceiling
        cam.walkMove(glm::vec3(0.0f, 0.0f, -2.0f));
        glm::vec3 eye = cam.walkEye();
        requireNear(eye.y, 1.7f, 1e-5f,
                    "forward movement while pitched up must not lift the eye");
        requireNear(eye.z, -2.0f, 1e-4f, "-Z local is forward at yaw 0");
        requireNear(eye.x, 0.0f, 1e-4f, "forward movement does not drift sideways");

        // A vertical component in the request is discarded outright.
        cam.walkMove(glm::vec3(0.0f, 5.0f, 0.0f));
        requireNear(cam.walkEye().y, 1.7f, 1e-5f,
                    "a vertical move request is ignored");

        // Strafing at 90 deg yaw runs along world Z, and still holds height.
        cam.enterWalk(glm::vec3(0.0f), 90.0f);
        cam.walkMove(glm::vec3(1.0f, 0.0f, 0.0f));
        eye = cam.walkEye();
        requireNear(eye.y, 1.7f, 1e-5f, "strafing must not change the eye height");
        require(std::abs(eye.z) > 0.5f,
                "strafe right at 90 deg yaw translates along world Z");
    }

    // Peeking at eye level must cost the author nothing: leaveWalk() restores
    // the orbit and fly cameras bit-for-bit.
    {
        EditorCamera cam;
        cam.frame(glm::vec3(4.0f, 1.0f, -2.0f), 17.5f);
        cam.orbit(0.3f, -0.2f);
        cam.setFlyPosition(glm::vec3(-8.0f, 3.0f, 11.0f));
        const glm::vec3 target = cam.target();
        const float distance = cam.distance();
        const float yaw = cam.yaw(), pitch = cam.pitch();
        const glm::vec3 fly = cam.flyEye();
        const glm::vec3 orbitEye = cam.eye();

        cam.enterWalk(glm::vec3(0.0f, 0.0f, 0.0f), 123.0f);
        require(cam.walking(), "enterWalk sets the walking flag");
        cam.walkLook(1.0f, 0.4f);
        cam.walkMove(glm::vec3(3.0f, 0.0f, -9.0f));
        cam.leaveWalk();

        require(!cam.walking(), "leaveWalk clears the walking flag");
        require(cam.target() == target, "leaveWalk restores the orbit target");
        require(cam.distance() == distance, "leaveWalk restores the orbit distance");
        require(cam.yaw() == yaw, "leaveWalk restores yaw exactly");
        require(cam.pitch() == pitch, "leaveWalk restores pitch exactly");
        require(cam.flyEye() == fly, "leaveWalk restores the fly position exactly");
        // Compared with == rather than an epsilon: the restored state is the
        // same bits fed through the same maths, so it must reproduce exactly.
        require(cam.eye() == orbitEye,
                "leaveWalk restores the derived orbit eye exactly");
    }

    // The preview exists to frame what the player will see, so its fov must not
    // silently become the editor's. If these ever legitimately converge, this
    // assert is the thing that forces the decision to be made deliberately.
    {
        require(EditorCamera().walkFovDeg() == EditorCamera::kGameFovDeg,
                "walk mode reports the game fov");
        requireNear(EditorCamera::kGameFovDeg, 70.0f, 1e-6f,
                    "game fov must track game.cameraFov in game/src/main.cpp");
        requireNear(EditorCamera::kEditorFovDeg, 65.0f, 1e-6f,
                    "editor fov must track the 65 deg used by EditorApp");
        require(EditorCamera::kGameFovDeg != EditorCamera::kEditorFovDeg,
                "the walk fov must differ from the editor fov");
    }

    // Every viewport consumer uses one active pose. Entering walk must switch
    // picking and gizmos along with rendering, then restore fly state on exit.
    {
        EditorCamera cam;
        cam.setFlyPosition(glm::vec3(9.0f, 8.0f, 7.0f));
        cam.setYawPitch(0.4f, -0.2f);
        const glm::vec3 flyEye = cam.flyEye();
        const glm::quat flyOrientation = cam.flyOrientation();
        require(cam.activeEye() == flyEye,
                "active eye starts on the fly camera");
        require(cam.activeOrientation() == flyOrientation,
                "active orientation starts on the fly camera");
        requireNear(cam.activeFovDeg(), EditorCamera::kEditorFovDeg, 1e-6f,
                    "fly camera uses editor fov");

        cam.enterWalk(glm::vec3(2.0f, 0.0f, -3.0f), 35.0f);
        require(cam.activeEye() == cam.walkEye(),
                "walk mode switches active eye");
        require(cam.activeOrientation() == cam.walkOrientation(),
                "walk mode switches active orientation");
        requireNear(cam.activeFovDeg(), EditorCamera::kGameFovDeg, 1e-6f,
                    "walk camera uses game fov");

        cam.leaveWalk();
        require(cam.activeEye() == flyEye,
                "leaving walk restores active eye exactly");
        require(cam.activeOrientation() == flyOrientation,
                "leaving walk restores active orientation exactly");
    }

    std::cout << "EditorCameraEyeTests OK\n";
    return 0;
}
