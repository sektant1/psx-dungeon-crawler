// Model viewer editor: a Unity-style asset preview. The Scene viewport shows
// the current model on a prototype-textured ground plane under an orbit camera,
// and the Inspector tweaks the selected node live (transform + material).

#include "EditorCamera.h"

#include <eng/Engine.h>
#include <eng/EditorUi.h>
#include <eng/LoadingScreen.h>
#include <eng/Log.h>
#include <eng/Renderer.h>
#include <eng/SceneView.h>

#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/editor.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();
    eng::LoadingScreen loading(engine);
    loading.begin("Loading editor");
    loading.step("Building preview stage", 0.3f);
    loading.present();

    // ---- preview stage: prototype-textured ground plane + lighting ---------
    const eng::NodeHandle sceneRoot =
        r.createNode(eng::kRootNode, glm::vec3(0.0f), "Editor Scene");
    {
        eng::NodeHandle floor =
            r.createNode(sceneRoot, glm::vec3(0.0f), "Prototype Grid");
        r.setScale(floor, glm::vec3(6.0f, 1.0f, 6.0f));
        r.attachMesh(floor, r.createPlane(1.0f), "Game/PrototypeFloor");
    }
    {
        eng::NodeHandle lightRoot =
            r.createNode(sceneRoot, glm::vec3(0.0f), "Lighting");
        eng::NodeHandle sun = r.createNode(lightRoot, glm::vec3(0.0f), "Key Light");
        r.setOrientation(sun, glm::angleAxis(glm::radians(40.0f), glm::vec3(0, 1, 0)) *
                                  glm::angleAxis(glm::radians(-50.0f), glm::vec3(1, 0, 0)));
        eng::LightDesc key;
        key.type = eng::LightDesc::Type::Directional;
        key.colour = glm::vec3(0.95f, 0.93f, 0.88f);
        r.attachLight(sun, key);
        eng::NodeHandle fillNode =
            r.createNode(lightRoot, glm::vec3(-3.0f, 3.0f, 3.0f), "Fill Light");
        eng::LightDesc fill;
        fill.type = eng::LightDesc::Type::Point;
        fill.colour = glm::vec3(0.30f, 0.34f, 0.42f);
        fill.range = 24.0f;
        r.attachLight(fillNode, fill);
    }
    r.setAmbient(glm::vec3(0.35f, 0.36f, 0.40f));
    r.setBackground({0.09f, 0.10f, 0.12f});
    r.setCameraFov(50.0f);
    r.setCameraClip(0.05f, 200.0f);

    // ---- swappable preview model ------------------------------------------
    eng::NodeHandle modelRoot;
    std::string currentMat = "Game/DungeonTile";
    EditorCamera cam;
    cam.orbit(0.6f, 0.9f); // three-quarter view, tipped down onto the plane
    bool autoSpin = true;  // Unity-like turntable until the user grabs the view
    const auto frameModel = [&] {
        glm::vec3 center(0.0f, 0.5f, 0.0f);
        float radius = 1.0f;
        if (modelRoot.valid())
            r.nodeWorldBounds(modelRoot, center, radius);
        cam.frame(center, std::max(1.2f, radius * 2.6f));
        autoSpin = true;
    };
    const auto setModel = [&](const std::string& objPath, const std::string& mat) {
        if (modelRoot.valid()) {
            r.destroyNode(modelRoot);
            modelRoot = eng::NodeHandle{};
        }
        const std::string name = std::filesystem::path(objPath).stem().string();
        modelRoot = r.createNode(sceneRoot, glm::vec3(0.0f), name);
        r.attachMesh(modelRoot, r.loadObj(objPath), mat, true);
        currentMat = mat;
        frameModel();
    };

    // Discover .obj models under the asset mesh folders for the model picker.
    std::vector<eng::EditorUi::SceneFile> models;
    const auto scan = [&](const std::string& dir) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return;
        std::vector<std::string> paths;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".obj")
                paths.push_back(e.path().string());
        std::sort(paths.begin(), paths.end());
        for (const auto& p : paths)
            models.push_back({std::filesystem::path(p).filename().string(), p});
    };
    scan(assets + "/meshes/props");
    scan(assets + "/meshes/tiles");

    int activeModel = 0;
    for (int i = 0; i < int(models.size()); ++i)
        if (models[i].label.find("chest") != std::string::npos) { activeModel = i; break; }
    loading.step("Loading model", 0.7f);
    loading.present();
    if (!models.empty())
        setModel(models[activeModel].path, currentMat);
    else
        eng::log::error("Editor: no .obj models found under %s/meshes", assets.c_str());

    // ---- viewport RTT + orbit camera --------------------------------------
    int vpW = 960, vpH = 640;
    r.enableEditorViewport(vpW, vpH);
    r.setEditorCameraPose(cam.eye(), cam.orientation(), 50.0f);

    engine.debugUi().setMainWindowVisible(false);
    engine.debugUi().setVisible(true);
    engine.input().setMouseGrab(false);

    // Launch the real game/demo in their own window (separate process).
    std::string exeDir = ".";
    {
        std::error_code ec;
        const auto self = std::filesystem::canonical("/proc/self/exe", ec);
        if (!ec) exeDir = self.parent_path().string();
    }
    const auto launch = [](const std::string& exe) {
        std::system(("SDL_VIDEODRIVER=x11 \"" + exe + "\" >/dev/null 2>&1 &").c_str());
    };
    const std::string gameExe = exeDir + "/game";
    const std::string demoExe = exeDir + "/psx_demo";

    eng::EditorUi ui(r);
    ui.setSceneFiles(models, activeModel);
    ui.setLoadSceneCallback([&](const eng::EditorUi::SceneFile& f) {
        setModel(f.path, currentMat);
    });
    engine.debugUi().addWindow([&] {
        ui.draw(r.editorViewportTextureId());
        const auto sz = ui.viewportSize();
        if (sz.w > 32 && sz.h > 32 && (sz.w != vpW || sz.h != vpH)) {
            vpW = sz.w;
            vpH = sz.h;
            r.resizeEditorViewport(vpW, vpH);
        }
        ImGui::Begin("Run");
        ImGui::TextUnformatted("Launch a live build in its own window:");
        if (ImGui::Button("Run Game")) launch(gameExe);
        ImGui::SameLine();
        if (ImGui::Button("Run Demo")) launch(demoExe);
        ImGui::End();
    });
    loading.step("Ready", 1.0f);
    loading.present();
    loading.finish();

    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        if (engine.input().wasPressed("quit"))
            engine.requestClose();

        const ImGuiIO& io = ImGui::GetIO();
        if (ui.viewportHovered()) {
            if (io.MouseDown[1]) {
                cam.orbit(-io.MouseDelta.x * 0.008f, -io.MouseDelta.y * 0.008f);
                autoSpin = false;
            }
            if (io.MouseWheel != 0.0f) {
                cam.dolly(io.MouseWheel);
                autoSpin = false;
            }
        }
        if (autoSpin)
            cam.orbit(dt * 0.3f, 0.0f);
        r.setEditorCameraPose(cam.eye(), cam.orientation(), 50.0f);

        // Selection AABB overlay: a ~1 m wire box at the selected node.
        std::vector<eng::Renderer::DebugLine> lines;
        const eng::NodeHandle selNode = ui.selected();
        if (selNode.valid()) {
            eng::NodeInfo info;
            if (r.scene().info(selNode, info)) {
                const glm::vec3 c = info.position;
                const float e = 0.5f;
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

        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
