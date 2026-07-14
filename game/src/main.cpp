// dungeon-crawler scaffold: FPS walk in a PSX-shaded test room.

#include "FpsController.h"

#include <eng/Engine.h>

#include <cmath>
#include <string>

namespace {
float lin(float srgb) { return std::pow(srgb, 2.2f); }
} // namespace

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/game.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();

    r.setCameraFov(70.0f);
    r.setCameraClip(0.05f, 100.0f);
    r.setAmbient(glm::vec3(0.12f));
    r.setFog({lin(0.05f), lin(0.05f), lin(0.08f)}, 0.08f);
    r.setBackground({0.02f, 0.02f, 0.03f});

    // Room: 10m interior cube centred at y=5 -> floor at y=0.
    const float roomSize = 10.0f;
    eng::MeshHandle room = r.createInteriorBox(roomSize, 1);
    r.attachMesh(r.createNode(eng::kRootNode, {0.0f, roomSize / 2.0f, 0.0f}),
                 room, "Game/Room");

    eng::LightDesc sun;
    sun.type = eng::LightDesc::Type::Directional;
    sun.colour = {0.9f, 0.9f, 1.0f};
    eng::NodeHandle sunNode = r.createNode(eng::kRootNode);
    r.setOrientation(sunNode,
                     glm::angleAxis(glm::radians(-60.0f), glm::vec3(1, 0, 0)));
    r.attachLight(sunNode, sun);

    for (float x : {-3.0f, 3.0f}) {
        eng::LightDesc lamp; // type defaults to Point
        lamp.colour =
            glm::vec3(lin(0.909804f), lin(0.803922f), lin(0.666667f)) * 4.75f;
        lamp.range = 5.0f;
        r.attachLight(r.createNode(eng::kRootNode, {x, 1.5f, 0.0f}), lamp);
    }

    r.setDitherEnabled(true);

    FpsController player;
    const float margin = roomSize / 2.0f - 0.5f;
    player.init(r, {0.0f, 0.0f, 3.0f},
                float(engine.config().getNumber("player.speed", 3.0)),
                float(engine.config().getNumber("player.mouse_sensitivity", 0.002)),
                {-margin, 0.0f, -margin}, {margin, 0.0f, margin});
    engine.input().setMouseGrab(true);

    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        eng::Input& in = engine.input();
        // First Esc releases the mouse, second quits; click re-grabs.
        if (in.wasPressed("quit")) {
            if (in.mouseGrabbed())
                in.setMouseGrab(false);
            else
                engine.requestClose();
        }
        if (!in.mouseGrabbed() && in.wasMouseClicked())
            in.setMouseGrab(true);

        player.update(in, r, dt);
        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
