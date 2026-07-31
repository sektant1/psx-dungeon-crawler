// Play an authored .map: load ECS scene via MapRuntime, resolve meshes against
// the renderer, build render + physics, then run an FPS walk loop. Reached from
// `game <file.map>`.

#include "MapPlay.h"

#include "GameCollision.h"
#include "scene/MapRuntime.h"
#include <eng/controllers/FpsController.h>

#include <eng/Input.h>
#include <eng/Log.h>
#include <eng/Physics.h>
#include <eng/Renderer.h>
#include <eng/app/FpsGameApp.h>
#include <eng/assets/AssetRoot.h>
#include <eng/ecs/Components.h>
#include <ecs/RendererSceneBackend.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <filesystem>
#include <optional>

namespace game {
namespace {

class MapPlayApp : public eng::FpsGameApp
{
public:
    explicit MapPlayApp(std::string mapPath) : mMapPath(std::move(mapPath)) {}

    eng::AppConfig configure(int, char**) override
    {
        eng::AppConfig cfg;
        cfg.mountSet = "game";
        cfg.configPath = "game.toml";
        // MapRuntime::step() owns the one physics.update per frame, so the
        // fixed loop stays off here and stepping happens in onPresent.
        cfg.fixedDt = 0.0f;
        cfg.imgui = true;
        return cfg;
    }

protected:
    eng::FpsGameConfig setup(eng::Engine&) override
    {
        eng::FpsGameConfig cfg;
        cfg.physics = game::layer::physicsSetup();
        cfg.staticLayers = eng::layerMask(game::layer::Static);
        cfg.phases = {"Step"};
        return cfg;
    }

    eng::FpsController* playerController() override { return &mPlayer; }

    bool onStartGame(eng::Engine& engine) override
    {
        eng::Renderer& r = engine.renderer();
        mBackend.emplace(r);
        mRuntime.emplace(*mBackend, physics());
        MapRuntime& rt = *mRuntime;

        if (!rt.load(mMapPath)) {
            eng::log::error("playMap: load failed: %s", mMapPath.c_str());
            exitCode = 1;
            return false;
        }

        // The editor stores absolute .obj paths in MeshSource (Palette scans
        // the game pack's meshes/{props,tiles} and persists path().string()).
        // Prefer the stored path as-is; fall back to the resolver, which is
        // what makes a portable map portable.
        rt.resolveMeshes([&](const std::string& path) -> eng::MeshHandle {
            std::error_code ec;
            if (std::filesystem::exists(path, ec))
                return r.loadObj(path);
            const std::filesystem::path alt = eng::assets::resolve(path);
            if (!alt.empty())
                return r.loadObj(alt.string());
            eng::log::error("playMap: mesh not found: %s", path.c_str());
            return r.prototypeMesh(path);
        });

        rt.buildAll();

        // If the map authored no lights, give the scene a directional key light
        // plus ambient/background so it is visible at all (mirrors the editor
        // default).
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
            eng::NodeHandle keyNode =
                r.createNode(eng::kRootNode, glm::vec3(0.0f, 6.0f, 0.0f));
            // A directional light aims down its node's local -Z. Rotate the node
            // so the key light rakes down onto floors (pitch it ~55 deg below
            // level) instead of shining flat along the horizon (which leaves
            // +Y-facing floors unlit and the scene black).
            r.setOrientation(keyNode,
                             glm::angleAxis(glm::radians(-55.0f),
                                            glm::vec3(1.0f, 0.0f, 0.0f)));
            r.attachLight(keyNode, key);
        }

        mPlayer.init(r, physics(), rt.playerSpawn(), 6.0f, 0.0025f,
                     glm::vec3(-500.0f), glm::vec3(500.0f));
        engine.input().setMouseGrab(true);
        return true;
    }

    // FpsController::update drives the character each frame; rt.step(dt) then
    // calls physics.update(dt) once and re-syncs scene + physics transforms --
    // exactly one physics.update per frame, after the controller has posted its
    // velocity for that character.
    void onPresent(const eng::FrameContext& f) override
    {
        eng::Renderer& r = f.engine.renderer();
        mPlayer.update(f.engine.input(), r, f.dt);
        mRuntime->step(f.dt);
        mPlayer.present(r);
    }

    void onStopGame(eng::Engine&) override
    {
        mRuntime.reset();
        mBackend.reset();
    }

private:
    std::string mMapPath;
    std::optional<eng::ecs::RendererSceneBackend> mBackend;
    std::optional<MapRuntime> mRuntime;
    eng::FpsController mPlayer;
};

} // namespace

int runMap(const std::string& mapPath)
{
    MapPlayApp app(mapPath);
    return eng::runApplication(app);
}

} // namespace game
