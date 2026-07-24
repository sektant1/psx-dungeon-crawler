// Play an authored .map: load ECS scene via MapRuntime, resolve meshes against
// the renderer, build render + physics, then run an FPS walk loop. Reached from
// `game <file.map>`.

#include "MapPlay.h"

#include "FpsController.h"
#include "scene/MapRuntime.h"

#include <eng/Engine.h>
#include <eng/Input.h>
#include <eng/Log.h>
#include <eng/Physics.h>
#include <eng/Renderer.h>
#include <eng/ecs/Components.h>
#include <ecs/RendererSceneBackend.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <filesystem>

namespace game {

int playMap(eng::Engine& engine, eng::Physics& physics,
            const std::string& assetDir, const std::string& mapPath)
{
    eng::Renderer& r = engine.renderer();
    r.setCameraFov(70.0f);
    r.setCameraClip(0.08f, 90.0f);
    eng::ecs::RendererSceneBackend backend(r);
    MapRuntime rt(backend, physics);

    if (!rt.load(mapPath)) {
        eng::log::error("playMap: load failed: %s", mapPath.c_str());
        return 1;
    }

    // The editor stores absolute .obj paths in MeshSource (Palette scans
    // <assetDir>/meshes/{props,tiles} and persists path().string()). Prefer the
    // stored path as-is; fall back to assetDir-relative for portable maps.
    rt.resolveMeshes([&](const std::string& path) -> eng::MeshHandle {
        std::error_code ec;
        if (std::filesystem::exists(path, ec))
            return r.loadObj(path);
        const std::string alt = assetDir + "/" + path;
        if (std::filesystem::exists(alt, ec))
            return r.loadObj(alt);
        eng::log::error("playMap: mesh not found: %s", path.c_str());
        return {}; // invalid handle -> SceneSync skips attach
    });

    rt.buildAll();

    // If the map authored no lights, give the scene a directional key light plus
    // ambient/background so it is visible at all (mirrors the editor default).
    bool hasLight = false;
    for (auto e : rt.registry().view<eng::ecs::LightRef>()) {
        (void)e;
        hasLight = true;
        break;
    }
    if (!hasLight) {
        r.setAmbient(glm::vec3(0.35f, 0.36f, 0.40f));
        r.setBackground(glm::vec3(0.09f, 0.10f, 0.12f));
        eng::LightDesc key;
        key.type = eng::LightDesc::Type::Directional;
        key.colour = glm::vec3(0.95f, 0.93f, 0.88f);
        eng::NodeHandle keyNode = r.createNode(eng::kRootNode, glm::vec3(0.0f, 6.0f, 0.0f));
        // A directional light aims down its node's local -Z. Rotate the node so
        // the key light rakes down onto floors (pitch it ~55 deg below level)
        // instead of shining flat along the horizon (which leaves +Y-facing
        // floors unlit and the scene black).
        r.setOrientation(keyNode, glm::angleAxis(glm::radians(-55.0f),
                                                 glm::vec3(1.0f, 0.0f, 0.0f)));
        r.attachLight(keyNode, key);
    }

    FpsController player;
    player.init(r, physics, rt.playerSpawn(), 6.0f, 0.0025f,
                glm::vec3(-500.0f), glm::vec3(500.0f));
    engine.input().setMouseGrab(true);

    // Loop mirrors main.cpp's phase ordering, adapted to MapRuntime. In main.cpp
    // physics.update runs first, then player.update (which sets character
    // velocity + runs characterUpdate internally). Here FpsController::update
    // still drives the character each frame; rt.step(dt) then calls
    // physics.update(dt) once and re-syncs scene + physics transforms. There is
    // no separate physics.update (main.cpp's is inside its fixed loop), so no
    // double-step occurs: exactly one physics.update per frame, after the
    // controller has posted its velocity for that character.
    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        eng::Input& in = engine.input();
        if (in.wasPressed("quit")) {
            if (in.mouseGrabbed())
                in.setMouseGrab(false);
            else
                engine.requestClose();
        }
        if (!in.mouseGrabbed() && in.wasMouseClicked())
            in.setMouseGrab(true);

        player.update(in, r, dt);
        rt.step(dt);
        player.present(r);
        engine.renderFrame(dt);
    }
    return 0;
}

} // namespace game
