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
#include <glm/gtc/quaternion.hpp> // glm::quat_cast

#include <algorithm>
#include <cmath>
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
    // sceneCam holds how this scene wants the viewport driven (fps/orbit/free);
    // the fps*/orbit* vars are the live per-frame runtime state, reseeded on load.
    eng::NodeHandle contentRoot;
    SceneCamera sceneCam;
    glm::vec3 fpsPos{0.0f};
    float fpsYaw = 0.0f, fpsPitch = 0.0f, orbitTime = 0.0f;
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
        sceneCam = SceneCamera{}; // default: free-fly editor camera
        if (f.path.empty()) {
            gen::Layout initial = gen::generate(1);
            if (!map.loadFromRows(r, physics, initial, tilesDir, propsDir, contentRoot))
                eng::log::error("Editor: procedural build failed");
            // A dungeon runs first-person, dropped at the generated spawn.
            sceneCam.mode = SceneCamera::Mode::Fps;
            sceneCam.eye = map.spawn() + glm::vec3(0.0f, 1.6f, 0.0f);
            sceneCam.fovDeg = 70.0f;
        } else {
            std::string error;
            if (!loadJsonScene(f.path, r, &physics, contentRoot, assets, error,
                               &map, &sceneCam))
                eng::log::error("Editor: load %s failed: %s", f.path.c_str(),
                                error.c_str());
        }
        // Reseed live camera runtime state from the scene's request.
        fpsPos = sceneCam.eye;
        fpsYaw = sceneCam.yaw;
        fpsPitch = 0.0f;
        orbitTime = 0.0f;
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
        // Drive the viewport camera the way the loaded scene runs: free-fly
        // editor eye, an auto-rotating turntable (demo), or a first-person
        // walk-through (game/dungeon). Input is read from ImGui IO and only
        // while the Scene viewport is hovered.
        const ImGuiIO& io = ImGui::GetIO();
        const bool hovered = ui.viewportHovered();
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        // Orientation looking from `eye` at `target` for an Ogre camera (which
        // faces local -Z). Avoids the experimental glm::quatLookAt.
        const auto lookAt = [](glm::vec3 eye, glm::vec3 target, glm::vec3 u) {
            const glm::vec3 f = glm::normalize(target - eye);
            const glm::vec3 rgt = glm::normalize(glm::cross(f, u));
            const glm::vec3 tu = glm::cross(rgt, f);
            return glm::quat_cast(glm::mat3(rgt, tu, -f));
        };
        glm::vec3 camEye = cam.eye();
        glm::quat camOrient = cam.orientation();
        float camFov = 60.0f;
        switch (sceneCam.mode) {
        case SceneCamera::Mode::Editor:
            if (hovered) {
                if (io.MouseDown[1])
                    cam.orbit(-io.MouseDelta.x * 0.008f, -io.MouseDelta.y * 0.008f);
                if (io.MouseWheel != 0.0f)
                    cam.dolly(io.MouseWheel);
            }
            camEye = cam.eye();
            camOrient = cam.orientation();
            break;
        case SceneCamera::Mode::Orbit: {
            orbitTime += dt;
            if (hovered && io.MouseWheel != 0.0f)
                sceneCam.distance =
                    std::clamp(sceneCam.distance - io.MouseWheel * 0.4f, 1.0f, 60.0f);
            const float yaw = sceneCam.yaw + orbitTime * sceneCam.orbitSpeed;
            const float cp = std::cos(sceneCam.orbitPitch);
            const float sp = std::sin(sceneCam.orbitPitch);
            camEye = sceneCam.target +
                     sceneCam.distance * glm::vec3(cp * std::sin(yaw), sp, cp * std::cos(yaw));
            camOrient = lookAt(camEye, sceneCam.target, up);
            camFov = sceneCam.fovDeg;
            break;
        }
        case SceneCamera::Mode::Fps: {
            if (hovered && io.MouseDown[1]) {
                fpsYaw -= io.MouseDelta.x * 0.0032f;
                fpsPitch = std::clamp(fpsPitch - io.MouseDelta.y * 0.0032f,
                                      -1.5f, 1.5f);
            }
            camOrient = glm::angleAxis(fpsYaw, up) *
                        glm::angleAxis(fpsPitch, glm::vec3(1, 0, 0));
            const glm::vec3 fwd = camOrient * glm::vec3(0, 0, -1);
            const glm::vec3 right = camOrient * glm::vec3(1, 0, 0);
            if (hovered) {
                glm::vec3 move(0.0f);
                if (ImGui::IsKeyDown(ImGuiKey_W)) move += fwd;
                if (ImGui::IsKeyDown(ImGuiKey_S)) move -= fwd;
                if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
                if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
                if (ImGui::IsKeyDown(ImGuiKey_E)) move += up;
                if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= up;
                if (glm::dot(move, move) > 0.0f) {
                    const float boost = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0f : 1.0f;
                    fpsPos += glm::normalize(move) * sceneCam.moveSpeed * boost * dt;
                }
            }
            camEye = fpsPos;
            camFov = sceneCam.fovDeg;
            break;
        }
        }
        r.setEditorCameraPose(camEye, camOrient, camFov);

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
