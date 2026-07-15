// dungeon-crawler scaffold: FPS walk in a PSX-shaded test room, with the
// demo scene (crystals, boxes, sparkles, light shaft) shared via
// samples/common/DemoScene + demo_scene.toml. Game-specific lighting
// overrides are applied after the scene loads.

#include "FpsController.h"

#include <DemoScene.h>

#include <imgui.h>

#include <eng/Engine.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/game.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();

    r.setCameraFov(70.0f);
    r.setCameraClip(0.05f, 100.0f);

    // ------------------------------------------------------------ Room ---
    // 10m interior box, "Game/Room" material, floor at y=0.
    const float roomSize = 10.0f;
    // dense subdiv: affine (noperspective) UVs warp badly on large tris
    eng::MeshHandle room = r.createInteriorBox(roomSize, 7);
    r.attachMesh(r.createNode(eng::kRootNode, {0.0f, roomSize / 2.0f, 0.0f}),
                 room, "Game/Room");

    // ------------------------------------------------------ shared scene ---
    // Sun parented to the root: static here, orbiting in the demo.
    DemoScene scene;
    if (!scene.load(r, DEMO_SCENE_TOML, assets + "/meshes/", eng::kRootNode))
        return 1;

    // ------------------------------------------- game lighting overrides ---
    const auto lin = [](float srgb) { return std::pow(srgb, 2.2f); };
    // Ambient raised to 0.25 (demo uses 0.15) so unlit wall areas remain
    // readable.
    r.setAmbient({lin(1.0f) * 0.25f, lin(0.67451f) * 0.25f,
                  lin(0.988235f) * 0.25f});
    // Steep top-down sun: the demo's rig orbits with the camera, a static
    // copy leaves the far walls unlit. ~75 degrees down, slight yaw so
    // adjacent walls differ.
    r.setOrientation(scene.sunNode(),
                     glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0)) *
                         glm::angleAxis(glm::radians(-75.0f), glm::vec3(1, 0, 0)));
    // Additive shaft oversaturates in the small room; dim it game-side.
    r.setMaterialParam("PSX/LightShaft", "modulateColor",
                       glm::vec4(1.0f, 1.0f, 1.0f, 0.35f));

    // ------------------------------------------------------- FPS player ---
    FpsController player;
    const float margin = roomSize / 2.0f - 0.5f;
    player.init(r, {0.0f, 0.0f, 3.0f},
                float(engine.config().getNumber("player.speed", 3.0)),
                float(engine.config().getNumber("player.mouse_sensitivity", 0.002)),
                {-margin, 0.0f, -margin}, {margin, 0.0f, margin});
    engine.input().setMouseGrab(true);

    engine.debugUi().addPanel("Player", [&player] {
        ImGui::SliderFloat("move speed", &player.speed(), 0.5f, 15.0f);
        ImGui::SliderFloat("mouse sensitivity", &player.sensitivity(), 0.0005f,
                           0.01f, "%.4f");
    });

    // ---------------------------------------------------------------- loop ---
    float animTime = 0.0f;
    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        eng::Input& in = engine.input();
        // First Esc releases the mouse, second quits; click re-grabs.
        // Suspended while the debug panel is open (F1 owns grab then).
        if (!engine.debugUi().visible()) {
            if (in.wasPressed("quit")) {
                if (in.mouseGrabbed())
                    in.setMouseGrab(false);
                else
                    engine.requestClose();
            }
            if (!in.mouseGrabbed() && in.wasMouseClicked())
                in.setMouseGrab(true);
        }

        animTime += dt;
        scene.update(r, animTime);

        player.update(in, r, dt);
        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
