// ogre-psx-demo -- port of MenacingMecha's godot-psx-style-demo, driven
// through the eng public API (no Ogre/SDL includes here). The scene itself
// is shared with the game: samples/common/DemoScene + demo_scene.toml.

#include "DemoScene.h"

#include <eng/Engine.h>
#include <eng/Math.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>

namespace {

// world/orbit_camera.gd: rotation.y = base + t
struct OrbitCamera {
    eng::NodeHandle node;
    float baseYaw = 0.0f;
    void update(eng::Renderer& r, float t) const
    {
        r.setOrientation(node, glm::angleAxis(baseYaw + t, glm::vec3(0, 1, 0)));
    }
};

} // namespace

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/demo.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();

    // Camera3D: fov 68.1243 vertical, Godot default clips.
    r.setCameraFov(68.1243f);
    r.setCameraClip(0.05f, 4000.0f);

    // -------------------------------------------------- OrbitPoint branch ---
    OrbitCamera orbit;
    orbit.node = r.createNode(eng::kRootNode);
    orbit.baseYaw = std::atan2(-0.556238f, 0.831023f);

    eng::NodeHandle camNode = r.createNode(orbit.node, {0.0f, 2.147f, 4.48151f});
    r.setOrientation(camNode,
                     eng::quatFromBasisRows(1, 0, 0, 0, 0.989078f, 0.147395f, 0,
                                            -0.147395f, 0.989078f));
    r.attachCamera(camNode);

    // --------------------------------------------------------- Background ---
    eng::MeshHandle bgMesh = r.createInteriorBox(40.0f, 25);
    r.attachMesh(r.createNode(eng::kRootNode, {0.0f, 20.0f, 0.0f}), bgMesh,
                 "PSX/Floor");

    // -------------------------------------------------------- shared scene ---
    DemoScene scene;
    if (!scene.load(r, DEMO_SCENE_TOML, assets + "/meshes/", orbit.node))
        return 1;

    // ---------------------------------------------------------------- loop ---
    bool paused = false;
    float animTime = 0.0f;
    orbit.update(r, 0.0f);

    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        eng::Input& in = engine.input();
        if (in.wasPressed("quit"))
            engine.requestClose();
        if (in.wasPressed("pause"))
            paused = !paused;
        if (in.wasPressed("restart"))
            animTime = 0.0f;

        if (!paused)
            animTime += dt;
        orbit.update(r, animTime);
        scene.update(r, animTime);

        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
