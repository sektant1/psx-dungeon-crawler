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
    r.setAmbient(glm::vec3(0.18f));
    r.setBackground({0.025f, 0.030f, 0.035f});
    r.setCameraFov(55.0f);
    r.setCameraClip(0.1f, 250.0f);

    EditorCamera cam;
    // Frame the dungeon: target the anchor room at the origin, back the camera
    // out and tilt down a little so the whole hall is visible on open.
    for (int i = 0; i < 40; ++i) cam.dolly(-1.0f);   // increase distance to ~32
    cam.orbit(0.0f, -0.35f);
    eng::NodeHandle camNode = r.createNode(eng::kRootNode, cam.eye(), "EditorCamera");
    r.setOrientation(camNode, cam.orientation());
    r.attachCamera(camNode);

    int vpW = 1280, vpH = 720;
    r.enableEditorViewport(vpW, vpH);

    engine.debugUi().setMainWindowVisible(false);
    engine.debugUi().setVisible(true);
    engine.input().setMouseGrab(false);

    eng::EditorUi ui(r);
    engine.debugUi().addWindow([&] {
        const eng::EditorUi::Size sz = ui.viewportSize();
        if (sz.w > 0 && sz.h > 0 && (sz.w != vpW || sz.h != vpH)) {
            vpW = sz.w; vpH = sz.h; r.resizeEditorViewport(vpW, vpH);
        }
        ui.draw(r.editorViewportTextureId());
    });

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
