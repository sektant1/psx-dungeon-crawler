#pragma once

namespace eng::ecs {

// A point of view. The entity's world transform is where the camera is and what
// it looks along (-Z forward, +Y up, like the renderer's own convention); this
// component is the lens.
//
// Making the camera an entity is what lets a shot be *authored* rather than
// coded. A camera that orbits a prop is a Camera parented to a pivot with a
// Spin on it -- two components and no C++ -- and the same rig works for a
// cutscene, a security monitor, a menu backdrop or a ten-second clip for a
// changelog. Before it, every framing in the engine was a call to
// Renderer::attachCamera from whichever system happened to own the frame.
//
// SceneSync picks exactly one to look through and pushes it to the renderer:
// the active camera with the highest `priority`. That rule is what makes an
// override composable -- a scene ships with its establishing shot at priority
// 0, and a debug fly-cam or a death-cam takes over by existing at 10, without
// either knowing about the other.
//
// It does not fight a game that drives its own camera: an application only ends
// up with an authored camera if its scene has one, and nothing in a dungeon
// level does.
struct Camera {
    // Vertical field of view. The renderer's own default is 70, which is what a
    // first-person game is framed at; a shot of a prop usually wants less.
    float fovDegrees = 70.0f;
    // Near plane. Small enough to stand inside a doorway, large enough to keep
    // depth precision: this is the value that decides whether a distant wall
    // z-fights, and it matters far more than the far plane does.
    float nearClip = 0.05f;
    float farClip = 200.0f;
    // Highest wins. Ties go to whichever the registry yields first, which is
    // stable for a given scene but is not a rule to rely on -- give a camera
    // that must win a priority.
    int priority = 0;
    // A camera that exists but is not looked through. Cheaper and far more
    // useful than deleting it: an author keeps three framings in the scene and
    // switches between them.
    bool active = true;
};

} // namespace eng::ecs
