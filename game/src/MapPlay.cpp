// Play an authored .map: load ECS scene via MapRuntime, resolve meshes against
// the renderer, build render + physics, then run an FPS walk loop. Reached from
// `game <file.map>`.

#include "MapPlay.h"

#include "GameAssets.h"
#include "GameCollision.h"
#include "ParticleCollider.h"
#include "SceneFactory.h"
#include "scene/ComponentRegistry.h"
#include "scene/GameComponents.h"
#include "scene/MapRuntime.h"
#include "scene/MapSerializer.h"
#include <eng/controllers/FpsController.h>

#include <eng/Input.h>
#include <eng/Log.h>
#include <eng/Physics.h>
#include <eng/RenderPresetInfo.h>
#include <eng/Renderer.h>
#include <eng/render/GifRecorder.h>
#include <eng/particles/ParticleLibrary.h>
#include <eng/app/FpsGameApp.h>
#include <eng/assets/AssetRoot.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/Systems.h>
#include <eng/script/ScriptHost.h>
#include <ecs/RendererSceneBackend.h>

#include <entt/entt.hpp>

#include <cstdio>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <filesystem>
#include <optional>

namespace game {
namespace {

// A portal at every authored Exit.
//
// The exit is the authored fact -- a position and the yaw the player leaves
// facing -- and the portal is how that fact is *presented*: a lit membrane
// inside a kit surround, assembled by createPortalProp. The campaign built one
// in LiveLevel and the authored-map path did not, so an exit placed in the
// editor played as a bare marker in an empty doorway. The editor is meant to be
// the way the game gets made; a thing you place there has to become the thing
// you see when you play.
//
// Built here rather than in the cooker on purpose: a portal is renderer state
// -- nodes, a light, a particle effect -- not entities, and baking it into the
// .map would freeze a presentation decision into level data.
//
// This is the *generated* path. An authored level does not need it: the kit now
// carries `portal_membrane` and the surround pieces, so a portal placed in the
// editor is ordinary entities -- a membrane with an eng::ecs::PortalParams on
// it, an arch, a light -- and every one of them is inspectable, tunable and
// different from the portal in the next room. An `exit` with no membrane beside
// it still gets this one, which is what keeps a bare marker playable.
void buildExitPortals(eng::Renderer& renderer, MapRuntime& runtime)
{
    // An authored portal wins. A scene that placed a membrane has said what its
    // portal is; generating a second one on top of the marker gave two portals
    // in one doorway, one of them untouchable from the editor. The marker keeps
    // its meaning either way -- it is where the level exits -- and only stops
    // carrying a presentation decision the author already made.
    if (!runtime.registry().view<const eng::ecs::PortalParams>().empty()) {
        eng::log::info("Level: authored portal -- no generated one");
        return;
    }
    const std::string kitMeshDir = game::assetDir("meshes/kit");
    int built = 0;
    const auto view =
        runtime.registry().view<const game::Exit, const eng::ecs::Transform>();
    for (const entt::entity entity : view) {
        const game::Exit& exit = view.get<const game::Exit>(entity);
        const eng::ecs::Transform& transform =
            view.get<const eng::ecs::Transform>(entity);

        PortalPropStyle style;
        style.yawDegrees = exit.yawDegrees;
        style.kitMeshDir = kitMeshDir;
        // The same green the campaign's descending portal uses: two levels that
        // read differently because one was authored and one was generated is
        // exactly the inconsistency the editor exists to remove.
        style.lightColour = {0.22f, 1.05f, 0.10f};
        style.lightRange = 8.5f;
        if (!createPortalProp(renderer, transform.position, style).valid()) {
            eng::log::warn("playMap: portal at the exit could not be built");
            continue;
        }
        ++built;
    }
    if (built > 0)
        eng::log::info("Level: %d exit portal(s)", built);
}

class MapPlayApp : public eng::FpsGameApp
{
public:
    explicit MapPlayApp(std::string mapPath) : mMapPath(std::move(mapPath)) {}

    void setRecording(std::optional<eng::RecordingOptions> options)
    {
        mRecording = std::move(options);
    }

    eng::AppConfig configure(int argc, char** argv) override
    {
        eng::AppConfig cfg;
        cfg.renderPreset = eng::renderPresetFromArgs(argc, argv);
        cfg.mountSet = "game";
        cfg.configPath = "config/game.toml";
        // This app steps physics itself in onPresent (one update per frame,
        // after the controller has posted its velocity), so the runner's fixed
        // loop stays off.
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

    void onLoadGame(eng::Engine& engine, eng::LoadPlan& plan) override
    {
        eng::Renderer& renderer = engine.renderer();
        plan.add("Kindling particle effects", [this, &renderer] {
            mParticlesReady = mParticles.load(
                renderer, game::assetPath("config/particles.toml"));
        });
    }

    bool onStartGame(eng::Engine& engine) override
    {
        if (!mParticlesReady) {
            eng::log::error("MapPlay: config/particles.toml failed to load");
            exitCode = 1;
            return false;
        }
        eng::Renderer& r = engine.renderer();
        mBackend.emplace(r);
        mWorld.attachRenderer(*mBackend);
        mWorld.attachPhysics(physics());
        mWorld.attachAudio(audio(), /*drivesListener=*/true);
        mParticleCollider.emplace(physics(), eng::kAllLayers);
        r.setParticleCollider(&*mParticleCollider);
        // One map, no transitions: everything it contributes lives as long as
        // the app does, so no lifetime group is needed.
        mRuntime.emplace(mWorld, 0u);
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
        eng::ModelImportOptions legacyImport;
        legacyImport.pivot = eng::PivotMode::Source;
        rt.resolveMeshes([&, legacyImport](const std::string& path) -> eng::MeshHandle {
            std::error_code ec;
            if (std::filesystem::exists(path, ec))
                return r.loadMesh(path, legacyImport);
            const std::filesystem::path alt = eng::assets::resolve(path);
            if (!alt.empty())
                return r.loadMesh(alt.string(), legacyImport);
            eng::log::error("playMap: mesh not found: %s", path.c_str());
            return r.prototypeMesh(path);
        });

        rt.resolvePrimitives(r);
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

        buildExitPortals(r, rt);

        // RAVEN_PLAY_FROM=x,y,z drops the player there instead of at the scene's
        // spawn. The editor sets it on F5 so a playtest starts at whatever the
        // author was looking at: the alternative is walking the level from the
        // entrance every time you adjust a room at the far end of it, which is
        // the single thing that makes people stop playtesting.
        glm::vec3 start = rt.playerSpawn();
        if (const char* from = std::getenv("RAVEN_PLAY_FROM")) {
            glm::vec3 parsed(0.0f);
            if (std::sscanf(from, "%f,%f,%f", &parsed.x, &parsed.y, &parsed.z) ==
                3) {
                start = parsed;
            } else {
                eng::log::warn("MapPlay: RAVEN_PLAY_FROM is not x,y,z: %s", from);
            }
        }
        mPlayer.init(r, physics(), start, 6.0f, 0.0025f, glm::vec3(-500.0f),
                     glm::vec3(500.0f));

        // A scene that authored its own camera is a *shot*, not a level: it
        // plays itself, and the player controller would fight it for the
        // renderer's one camera every frame. So the controller stands down and
        // the mouse stays free -- which is also what makes such a scene
        // recordable with --record without a hand on the mouse.
        mCinematic = !mWorld.registry().view<eng::ecs::Camera>().empty();
        if (mCinematic)
            eng::log::info("Level: authored camera -- playing as a shot");
        engine.input().setMouseGrab(!mCinematic);
        // Last, so the clip's first frame is a fully built level: recording
        // pins the frame delta, and a load hitch would otherwise be baked into
        // the timing of the whole clip.
        // Scripts last, so start() sees the fully built level. The registry is
        // mapio::coreRegistry() -- the same table the serialiser and the editor
        // inspector use -- so every component the game registers is reachable
        // from Lua without a line of binding code.
        eng::script::ScriptConfig scriptConfig;
        scriptConfig.hotReload = true; // dev builds; see docs/scripting.md
        mScripts.emplace(mWorld, scriptConfig, mapio::coreRegistry());
        mScripts->bindInput(engine.input());
        mScripts->bindPhysics(physics());
        eng::script::registerScriptCommands(devConsole(), *mScripts);

        // Last, so the clip's first frame is a fully built level: recording
        // pins the frame delta, and a load hitch would otherwise be baked into
        // the timing of the whole clip.
        if (mRecording)
            engine.startRecording(*mRecording);
        return true;
    }

    // Exactly one physics.update per frame, after the controller has posted
    // its velocity, then one world sync to push the results at the renderer and
    // the bodies.
    //
    // The script callbacks bracket that step, and the order is the contract:
    //
    //   fixed_update              immediately before the step it influences
    //   Physics::update
    //   on_collision/on_trigger   right after, so a script reacts to a hit in
    //                             the same frame it happened
    //   update                    with the rest of presentation
    //   tickComponentSystems      then sync, so everything a script wrote this
    //   World::sync               frame is what gets pushed at the renderer
    //
    // This mode has no fixed loop -- it steps physics from here -- which is
    // exactly why fixedTick is defined as "before a physics step" rather than
    // "on the fixed clock".
    void onPresent(const eng::FrameContext& f) override
    {
        eng::Renderer& r = f.engine.renderer();
        if (!mCinematic)
            mPlayer.update(f.engine.input(), r, f.dt);

        if (mScripts) mScripts->fixedTick(f.dt);
        physics().update(f.dt);
        if (mScripts) {
            mScripts->drainContacts();
            mScripts->pollReload();
            mScripts->tick(f.dt);
        }

        // Component-driven motion -- spin, light animation, lifetimes -- before
        // the sync that pushes it. This is what makes an authored scene move
        // without a line of C++ per scene.
        eng::ecs::tickComponentSystems(mWorld, f.dt);
        mWorld.sync();
        if (!mCinematic)
            mPlayer.present(r);
    }

    void onStopGame(eng::Engine& engine) override
    {
        engine.renderer().setParticleCollider(nullptr);
        // Before the world and physics go: the host holds a contact
        // subscription on one and an on_destroy hook on the other.
        mScripts.reset();
        mRuntime.reset();
        // Nodes and bodies die before the renderer and the physics world do.
        mWorld.detachAll();
        mBackend.reset();
        mParticleCollider.reset();
    }

private:
    std::string mMapPath;
    std::optional<eng::ecs::RendererSceneBackend> mBackend;
    eng::ecs::World mWorld;
    std::optional<MapRuntime> mRuntime;
    // Constructed in onStartGame, once the world and physics exist. optional
    // rather than a plain member because a ScriptHost binds to a World for its
    // whole life.
    std::optional<eng::script::ScriptHost> mScripts;
    eng::ParticleLibrary mParticles;
    std::optional<game::JoltParticleCollider> mParticleCollider;
    bool mParticlesReady = false;
    eng::FpsController mPlayer;
    std::optional<eng::RecordingOptions> mRecording; // --record
    // The scene owns the camera: set once at load, because a camera appearing
    // mid-play would take the view away from a player already walking around.
    bool mCinematic = false;
};

} // namespace

bool mapHasCamera(const std::string& mapPath)
{
    entt::registry parsed;
    if (!mapio::readMap(mapPath, parsed, mapio::coreRegistry()))
        return false;
    return !parsed.view<eng::ecs::Camera>().empty();
}

int runMap(const std::string& mapPath, int argc, char** argv)
{
    MapPlayApp app(mapPath);
    app.setRecording(eng::GifRecorder::optionsFromArgs(argc, argv));
    return eng::runApplication(app, argc, argv);
}

} // namespace game
