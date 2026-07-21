#include "DungeonGen.h"
#include "DungeonMap.h"
#include "LevelEditor.h"

#include <eng/Engine.h>
#include <eng/Physics.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <string>

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/editor.toml", assets))
        return 1;

    eng::Renderer& renderer = engine.renderer();
    eng::Physics physics;
    physics.init();
    DungeonMap map;
    gen::Layout initial = gen::generate(1);
    if (!map.loadFromRows(renderer, physics, initial,
                          assets + "/meshes/tiles/",
                          assets + "/meshes/props/"))
        return 1;

    // A quiet preview remains behind the full-screen authoring workspace and
    // is ready if the UI later gains a dedicated 3D preview pane.
    renderer.setAmbient(glm::vec3(0.18f));
    renderer.setBackground({0.025f, 0.030f, 0.035f});
    renderer.setCameraFov(55.0f);
    renderer.setCameraClip(0.1f, 250.0f);
    eng::NodeHandle camera = renderer.createNode(eng::kRootNode, {0, 65, 0});
    renderer.setOrientation(camera,
                            glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0)));
    renderer.attachCamera(camera);

    LevelEditor editor(initial.rows(), assets + "/editor_level.toml", true);
    engine.debugUi().setMainWindowVisible(false);
    engine.debugUi().setVisible(true);
    engine.input().setMouseGrab(false);
    engine.debugUi().addWindow([&] {
        if (!editor.draw(map, glm::vec3(0.0f)))
            return;
        gen::Layout layout = editor.takeLayout();
        map.clearPhysics();
        renderer.clearScene();
        map.loadFromRows(renderer, physics, std::move(layout),
                         assets + "/meshes/tiles/", assets + "/meshes/props/");
    });

    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        if (engine.input().wasPressed("quit"))
            engine.requestClose();
        map.update(renderer, 0.0f);
        engine.renderFrame(dt);
    }
    map.clearPhysics();
    physics.shutdown();
    engine.shutdown();
    return 0;
}
