#include "DungeonGen.h"
#include "DungeonMap.h"
#include "EditorCamera.h"

#include <eng/Engine.h>
#include <eng/Physics.h>
#include <eng/Renderer.h>
#include <eng/SceneView.h>
#include <eng/EditorUi.h>

#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/editor.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();
    eng::Physics physics; physics.init();

    DungeonMap map;
    gen::Layout initial = gen::generate(1);
    if (!map.loadFromRows(r, physics, initial, assets + "/meshes/tiles/",
                          assets + "/meshes/props/"))
        return 1;
    r.setAmbient(glm::vec3(0.55f));
    r.setBackground({0.05f, 0.06f, 0.08f});
    r.setCameraFov(60.0f);
    r.setCameraClip(0.1f, 400.0f);
    // A key directional light so editor geometry reads with form/shading rather
    // than flat ambient. Steep angle keeps floors and wall tops both lit.
    {
        eng::NodeHandle sun = r.createNode(eng::kRootNode, {0.0f, 0.0f, 0.0f}, "Sun");
        r.setOrientation(sun, glm::angleAxis(glm::radians(35.0f), glm::vec3(0, 1, 0)) *
                                  glm::angleAxis(glm::radians(-55.0f), glm::vec3(1, 0, 0)));
        eng::LightDesc d;
        d.type = eng::LightDesc::Type::Directional;
        d.colour = glm::vec3(0.9f, 0.88f, 0.82f);
        r.attachLight(sun, d);
    }

    EditorCamera cam;
    // Frame the whole dungeon from a high three-quarter angle (target the anchor
    // room at the origin), like an editor's default overview shot.
    for (int i = 0; i < 34; ++i) cam.dolly(-1.0f);   // distance ~29
    // Positive pitch lifts the eye ABOVE the dungeon (sin(pitch) drives eye.y);
    // the default pitch is negative, so orbit up past horizontal to look down.
    cam.orbit(0.7f, 1.0f);
    eng::NodeHandle camNode = r.createNode(eng::kRootNode, cam.eye(), "EditorCamera");
    r.setOrientation(camNode, cam.orientation());
    r.attachCamera(camNode);

    engine.debugUi().setMainWindowVisible(false);
    engine.debugUi().setVisible(true);
    engine.input().setMouseGrab(false);

    // The editor uses a passthrough central dock node: the engine renders the
    // scene to the OS window as usual, and the docked panels frame a transparent
    // hole through which that render shows. No render-to-texture needed.
    eng::EditorUi ui(r);
    engine.debugUi().addWindow([&] { ui.draw(0); });

    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        if (engine.input().wasPressed("quit"))
            engine.requestClose();
        // Camera control from ImGui IO while the Scene viewport is hovered.
        if (ui.viewportHovered()) {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.MouseDown[1])
                cam.orbit(-io.MouseDelta.x * 0.008f, -io.MouseDelta.y * 0.008f);
            if (io.MouseWheel != 0.0f)
                cam.dolly(io.MouseWheel);
        }
        r.setPosition(camNode, cam.eye());
        r.setOrientation(camNode, cam.orientation());

        // Selection AABB overlay: a ~1 m wire box at the selected node.
        std::vector<eng::Renderer::DebugLine> lines;
        const eng::NodeHandle selNode = ui.selected();
        if (selNode.valid()) {
            eng::NodeInfo info;
            if (r.scene().info(selNode, info)) {
                const glm::vec3 c = info.position; const float e = 0.5f;
                const glm::vec3 col(0.55f, 0.8f, 1.0f);
                const glm::vec3 mn = c - glm::vec3(e), mx = c + glm::vec3(e);
                const glm::vec3 v[8] = {
                    {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},{mx.x,mn.y,mx.z},{mn.x,mn.y,mx.z},
                    {mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z},{mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z}};
                const int edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
                for (auto& ed : edges) lines.push_back({v[ed[0]], v[ed[1]], col});
            }
        }
        r.setDebugLines(lines);

        map.update(r, 0.0f);
        engine.renderFrame(dt);
    }
    map.clearPhysics(); physics.shutdown(); engine.shutdown();
    return 0;
}
