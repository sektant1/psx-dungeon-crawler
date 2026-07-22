#include "DungeonGen.h"
#include "DungeonMap.h"
#include "EditorCamera.h"
#include "JsonSceneLoader.h"

#include <eng/Engine.h>
#include <eng/LoadingScreen.h>
#include <eng/Log.h>
#include <eng/Physics.h>
#include <eng/Renderer.h>
#include <eng/SceneView.h>
#include <eng/EditorUi.h>

#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>
#include <filesystem>

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/editor.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();
    eng::Physics physics; physics.init();
    eng::LoadingScreen loading(engine);
    loading.begin("Loading editor");
    loading.step("Creating editor scene", 0.10f);
    loading.present();

    const eng::NodeHandle sceneRoot =
        r.createNode(eng::kRootNode, glm::vec3(0.0f), "Editor Scene");

    DungeonMap map;
    const std::string tilesDir = assets + "/meshes/tiles/";
    const std::string propsDir = assets + "/meshes/props/";

    // All openable content lives under a swappable root so the Scene picker can
    // reload it without disturbing the persistent lighting/camera rig below.
    eng::NodeHandle contentRoot;
    auto loadScene = [&](const eng::EditorUi::SceneFile& f) {
        if (contentRoot.valid()) {
            r.destroyNode(contentRoot);
            contentRoot = eng::NodeHandle{};
        }
        // Reset physics so repeated reloads don't accumulate stale bodies.
        map.clearPhysics();
        physics.shutdown();
        physics.init();
        contentRoot = r.createNode(sceneRoot, glm::vec3(0.0f),
                                   f.label.empty() ? "Content" : f.label);
        if (f.path.empty()) {
            gen::Layout initial = gen::generate(1);
            if (!map.loadFromRows(r, physics, initial, tilesDir, propsDir, contentRoot))
                eng::log::error("Editor: procedural build failed");
        } else {
            std::string error;
            if (!loadJsonScene(f.path, r, &physics, contentRoot, assets, error))
                eng::log::error("Editor: load %s failed: %s", f.path.c_str(),
                                error.c_str());
        }
    };

    // Discover selectable scenes: the built-in procedural dungeon, plus every
    // *.json in the asset root and assets/scenes/.
    std::vector<eng::EditorUi::SceneFile> sceneFiles;
    sceneFiles.push_back({"Procedural Dungeon", ""});
    const auto scanJson = [&](const std::string& dir) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".json")
                sceneFiles.push_back(
                    {e.path().filename().string(), e.path().string()});
    };
    scanJson(assets);
    scanJson(assets + "/scenes");

    // Default to scene.json if present, else the procedural dungeon.
    int activeScene = 0;
    for (int i = 0; i < int(sceneFiles.size()); ++i)
        if (sceneFiles[i].label == "scene.json") { activeScene = i; break; }

    loading.step("Building scene", 0.35f);
    loading.present();
    loadScene(sceneFiles[activeScene]);

    r.setAmbient(glm::vec3(0.55f));
    r.setBackground({0.05f, 0.06f, 0.08f});
    r.setCameraFov(60.0f);
    r.setCameraClip(0.1f, 400.0f);
    // A key directional light so editor geometry reads with form/shading rather
    // than flat ambient. Steep angle keeps floors and wall tops both lit.
    {
        const eng::NodeHandle lighting =
            r.createNode(sceneRoot, glm::vec3(0.0f), "Lighting");
        eng::NodeHandle sun = r.createNode(lighting, {0.0f, 0.0f, 0.0f}, "Sun");
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
    for (int i = 0; i < 70; ++i) cam.dolly(-1.0f);   // distance ~47
    // Positive pitch lifts the eye ABOVE the dungeon (sin(pitch) drives eye.y);
    // the default pitch is negative, so orbit up past horizontal to look down.
    cam.orbit(0.7f, 1.0f);

    int vpW = 960, vpH = 640;
    loading.step("Creating scene viewport", 0.75f);
    loading.present();
    r.enableEditorViewport(vpW, vpH);
    // The editor viewport has its OWN dedicated camera (a free-fly editor eye),
    // rendered into the docked Scene panel's RTT -- independent of the game's
    // MainCamera. Seed its pose from the framing EditorCamera above.
    r.setEditorCameraPose(cam.eye(), cam.orientation(), 60.0f);

    engine.debugUi().setMainWindowVisible(false);
    engine.debugUi().setVisible(true);
    engine.input().setMouseGrab(false);

    eng::EditorUi ui(r);
    ui.setSceneFiles(sceneFiles, activeScene);
    ui.setLoadSceneCallback([&](const eng::EditorUi::SceneFile& f) { loadScene(f); });
    engine.debugUi().addWindow([&] {
        ui.draw(r.editorViewportTextureId());
        const auto sz = ui.viewportSize();
        if (sz.w > 32 && sz.h > 32 && (sz.w != vpW || sz.h != vpH)) {
            vpW = sz.w;
            vpH = sz.h;
            r.resizeEditorViewport(vpW, vpH);
        }
    });
    loading.step("Ready", 1.0f);
    loading.present();
    loading.finish();

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
        r.setEditorCameraPose(cam.eye(), cam.orientation(), 60.0f);

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
