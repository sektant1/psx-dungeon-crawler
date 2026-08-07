#include <eng/runtime/ProjectApp.h>

#include <eng/Input.h>
#include <eng/Log.h>
#include <eng/Physics.h>
#include <eng/RenderPresetInfo.h>
#include <eng/Renderer.h>
#include <eng/assets/AssetRoot.h>
#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/Systems.h>

#include <imgui.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace eng::runtime {
namespace {

// A scene that authored no lights would render as a black window, which reads
// as a broken build rather than as an unlit scene. So an unlit scene gets the
// same default key the editor shows it under: whatever somebody just made is
// visible the first time they press play.
void lightIfDark(Renderer& r, const entt::registry& reg)
{
    if (!reg.view<const ecs::LightRef>().empty())
        return;

    r.setAmbient(glm::vec3(0.35f, 0.36f, 0.40f));
    r.setBackground(glm::vec3(0.09f, 0.10f, 0.12f));
    LightDesc key;
    key.type = LightDesc::Type::Directional;
    key.colour = glm::vec3(0.95f, 0.93f, 0.88f);
    const NodeHandle node = r.createNode(kRootNode, glm::vec3(0.0f, 6.0f, 0.0f));
    // A directional light aims down its node's local -Z. Rake it down onto the
    // floor rather than along the horizon, which would leave every +Y face --
    // i.e. the ground somebody is standing on -- unlit.
    r.setOrientation(node, glm::angleAxis(glm::radians(-55.0f),
                                          glm::vec3(1.0f, 0.0f, 0.0f)));
    r.attachLight(node, key);
}

// RAVEN_PLAY_FROM=x,y,z drops the player there instead of at the scene's
// spawn. The editor sets it on F5 so a playtest starts at whatever the author
// was looking at: the alternative is walking the level from the entrance every
// time you adjust a room at the far end of it, which is the single thing that
// makes people stop playtesting.
glm::vec3 applyPlayFromOverride(glm::vec3 start)
{
    const char* from = std::getenv("RAVEN_PLAY_FROM");
    if (!from)
        return start;
    glm::vec3 parsed(0.0f);
    if (std::sscanf(from, "%f,%f,%f", &parsed.x, &parsed.y, &parsed.z) == 3)
        return parsed;
    log::warn("ProjectApp: RAVEN_PLAY_FROM is not x,y,z: %s", from);
    return start;
}

} // namespace

ProjectApp::ProjectApp(Project project) : mProject(std::move(project)) {}

void ProjectApp::setRecording(std::optional<RecordingOptions> options)
{
    mRecording = std::move(options);
}

const ecs::ComponentRegistry& ProjectApp::components() const
{
    // The engine's components plus whatever the project declared in
    // components.toml. Built once, lazily, because configure() runs before
    // anything has mounted the project and this is first asked for at scene
    // load -- and cached, because the table is what every scene read and every
    // Lua component access walks.
    if (!mComponentsBuilt) {
        auto* self = const_cast<ProjectApp*>(this);
        self->mComponentsBuilt = true;
        ecs::registerEngineComponents(self->mComponents);

        if (!mProject.dir.empty()) {
            std::string error;
            const std::filesystem::path file =
                mProject.dir / ProjectComponents::kFileName;
            if (!self->mDeclared.load(file, error)) {
                // Refused, not partially applied: a component that silently
                // lost a field would corrupt every scene saved after it. The
                // project still runs, without its own components, which is far
                // easier to diagnose than a scene that decodes wrongly.
                log::error("ProjectApp: %s", error.c_str());
            } else if (!self->mDeclared.registerInto(self->mComponents, error)) {
                log::error("ProjectApp: %s", error.c_str());
            }
        }
    }
    return mComponents;
}

AppConfig ProjectApp::configure(int argc, char** argv)
{
    AppConfig cfg;
    // The command line still wins: --preset is how somebody compares looks
    // without editing the file they are comparing.
    cfg.renderPreset = renderPresetFromArgs(argc, argv);
    if (cfg.renderPreset == 0 && !mProject.renderPreset.empty())
        cfg.renderPreset = renderPresetFromName(mProject.renderPreset.c_str());
    cfg.mountSet = "game";
    cfg.projectDir = mProject.dir.string();
    // The project's own file is its config: [window] and [bindings] are read
    // straight out of it, so there is one place to change what the window is
    // called and what W does. It resolves through the project pack, which
    // Engine::init mounts over the builtin one before this is looked up.
    //
    // A bare .map -- the game's `game <file.map>` path -- has no project
    // directory and falls back to the engine's own config.
    cfg.configPath = mProject.dir.empty() ? "config/game.toml" : kProjectFile;
    // This app steps physics itself in onPresent (one update per frame, after
    // the controller has posted its velocity), so the runner's fixed loop
    // stays off.
    cfg.fixedDt = 0.0f;
    cfg.imgui = true;
    cfg.loadingTitle = mProject.name;
    return cfg;
}

FpsGameConfig ProjectApp::setup(Engine&)
{
    FpsGameConfig cfg;
    cfg.phases = {"Step"};
    return cfg;
}

std::string ProjectApp::scenePath() const
{
    // An explicit map wins: this is how the editor plays the scene on screen
    // rather than the one the project calls its main.
    if (const char* override = std::getenv("RAVEN_PLAY_MAP")) {
        if (*override)
            return override;
    }
    if (mProject.dir.empty())
        return mProject.mainScene;

    const std::filesystem::path cooked = mProject.cookedMainScene();
    std::error_code ec;
    if (std::filesystem::is_regular_file(cooked, ec))
        return cooked.string();

    // Nothing cooked yet. Say which file was expected rather than reporting an
    // empty scene: "cook it" is the fix, and the message has to imply it.
    log::error("ProjectApp: '%s' has not been cooked (expected %s)",
               mProject.mainScene.c_str(), cooked.c_str());
    return {};
}

std::string ProjectApp::cookedPathFor(const std::string& scene) const
{
    // A script names a scene the way an author does -- "scenes/level2.scn" --
    // and the runtime is what knows the cooked form lives under .raven/cooked.
    // A path that is already a .map is taken as-is, which is what makes a bare
    // .map runnable and lets a game hand its own cooked file straight in.
    std::filesystem::path named(scene);
    if (named.extension() == ".map") {
        std::error_code ec;
        if (std::filesystem::is_regular_file(named, ec))
            return named.string();
        if (!mProject.dir.empty())
            return (mProject.dir / named).string();
        return named.string();
    }
    if (mProject.dir.empty()) {
        log::error("ProjectApp: '%s' cannot be resolved without a project",
                   scene.c_str());
        return {};
    }
    return (mProject.workDir() / "cooked" / (named.stem().string() + ".map"))
        .string();
}

void ProjectApp::applyPendingScene(Engine& engine)
{
    if (mPendingScene.empty())
        return;
    const std::string requested = mPendingScene;
    mPendingScene.clear();

    const std::string path = cookedPathFor(requested);
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) {
        // Refuse rather than tear the level down: a script with a typo in a
        // scene name should leave the player standing in the level they were
        // in, with an error naming the file, not in an empty world.
        log::error("ProjectApp: game.load_scene('%s') -- no cooked scene at %s",
                   requested.c_str(), path.c_str());
        return;
    }

    // Order matters. The host goes first: it holds instances whose entities are
    // about to be destroyed, and on_destroy has to fire while the world is
    // still whole. Then the group, which takes exactly the old scene's entities
    // and leaves anything the game spawned outside it alone.
    mScripts.reset();
    // Everything the outgoing scene spawned goes with it. Their handles lived
    // in scripts that are being torn down, so anything left here could never be
    // despawned by anybody again.
    for (const uint32_t group : mSpawnedGroups)
        mWorld.destroyGroup(group);
    mSpawnedGroups.clear();
    mWorld.destroyGroup(mSceneGroup);
    mScene.reset();
    mSceneGroup = nextGroup();

    if (!buildScene(engine, path)) {
        log::error("ProjectApp: '%s' failed to build; the world is now empty",
                   requested.c_str());
        return;
    }

    // The rig's nodes are the renderer's, not the world's: it creates them
    // with createNode directly, so destroyGroup above did not touch them and
    // init() below would overwrite the handles and orphan them under the root.
    // Ten door transitions would leave twenty nodes nothing can ever reach.
    //
    // detach(), not forgetNodes(): the nodes are still alive here. forgetNodes
    // is for the other case -- a scene clear that already destroyed them, which
    // is what game/src/PlayerSystem.cpp is guarding against.
    mPlayer.cameraRig().detach(engine.renderer());
    mPlayer.init(engine.renderer(), physics(), playerStart(*mScene), 6.0f,
                 0.0025f, glm::vec3(-500.0f), glm::vec3(500.0f));
    adoptSceneCamera(engine);
    startScripts(engine);
    log::info("ProjectApp: loaded scene '%s'", requested.c_str());
}

void ProjectApp::onLoadGame(Engine& engine, LoadPlan& plan)
{
    Renderer& renderer = engine.renderer();
    plan.add("Kindling particle effects", [this, &renderer] {
        // Optional, unlike in the game: a project that ships no particle
        // library is a project that has not needed one yet, and refusing to
        // start over it would be absurd. The game's own library resolves out
        // of the builtin pack, so nothing about that path changes.
        const std::filesystem::path path =
            assets::resolve("config/particles.toml");
        if (path.empty()) {
            log::info("ProjectApp: no config/particles.toml; none loaded");
            return;
        }
        mParticlesReady = mParticles.load(renderer, path.string());
        if (!mParticlesReady)
            log::warn("ProjectApp: config/particles.toml failed to load");
    });
}

MeshHandle ProjectApp::loadSceneMesh(Renderer& r, const std::string& path) const
{
    // The editor stores absolute .obj paths in MeshSource. Prefer the stored
    // path as-is; fall back to the resolver, which is what makes a portable
    // scene portable -- and what lets a project mount its own meshes over the
    // builtin ones.
    ModelImportOptions legacyImport;
    legacyImport.pivot = PivotMode::Source;
    std::error_code ec;
    if (std::filesystem::exists(path, ec))
        return r.loadMesh(path, legacyImport);
    const std::filesystem::path alt = assets::resolve(path);
    if (!alt.empty())
        return r.loadMesh(alt.string(), legacyImport);
    log::error("ProjectApp: mesh not found: %s", path.c_str());
    return r.prototypeMesh(path);
}

bool ProjectApp::buildScene(Engine& engine, const std::string& path)
{
    Renderer& r = engine.renderer();
    mScene.emplace(mWorld, mSceneGroup, components());
    SceneRuntime& scene = *mScene;
    if (!scene.load(path)) {
        log::error("ProjectApp: load failed: %s", path.c_str());
        return false;
    }

    // The editor stores absolute .obj paths in MeshSource. Prefer the stored
    // path as-is; fall back to the resolver, which is what makes a portable
    // scene portable -- and what lets a project mount its own meshes over the
    // builtin ones.
    scene.resolveMeshes([this, &r](const std::string& mesh) {
        return loadSceneMesh(r, mesh);
    });

    scene.resolvePrimitives(r);
    scene.buildAll([this](entt::registry& reg) { onBeforeSync(reg); });
    lightIfDark(r, scene.registry());
    onSceneBuilt(engine, scene);
    return true;
}

void ProjectApp::startScripts(Engine& engine)
{
    // Scripts last, so start() sees the fully built scene. The registry is the
    // same table the scene was read with, so every component the application
    // registers is reachable from Lua without a line of binding code.
    script::ScriptConfig scriptConfig;
    scriptConfig.root = mProject.scriptRoot;
    scriptConfig.hotReload = true; // dev builds; see docs/scripting.md
    mScripts.emplace(mWorld, scriptConfig, components());
    mScripts->bindInput(engine.input());
    mScripts->bindPhysics(physics());
    mScripts->bindAudio(audio());

    // Everything the script layer cannot answer for itself. The camera hooks
    // read the controller rather than the renderer, because the controller is
    // what actually decides where the eye is -- the renderer only receives it.
    script::RuntimeHooks hooks;
    hooks.loadScene = [this](const std::string& scene) {
        // Recorded, not acted on: the caller is a script instance that the
        // switch is about to destroy.
        mPendingScene = scene;
    };
    hooks.quit = [&engine]() { engine.requestClose(); };
    // Spawning names a scene the way an author does; cookedPathFor is what
    // turns that into the build product, exactly as load_scene does -- so a
    // script says "scenes/torch.scn" in both and never learns where .raven is.
    hooks.spawnScene = [this](const std::string& scene,
                              const glm::vec3& at) -> uint32_t {
        if (!mScene)
            return 0;
        const std::string path = cookedPathFor(scene);
        if (path.empty())
            return 0;
        const uint32_t group = nextGroup();
        if (!mScene->instantiate(path, at, group))
            return 0;
        mSpawnedGroups.push_back(group);
        return group;
    };
    hooks.despawn = [this](uint32_t group) {
        // Only what spawn_scene handed out. The scene's own group comes from
        // the same allocator, so a script holding a stale handle -- or simply
        // guessing -- could otherwise tear the level down with no error.
        if (std::find(mSpawnedGroups.begin(), mSpawnedGroups.end(), group) ==
            mSpawnedGroups.end()) {
            log::warn("Script: game.despawn(%u) is not a live spawn", group);
            return;
        }
        mWorld.destroyGroup(group);
        mSpawnedGroups.erase(
            std::remove(mSpawnedGroups.begin(), mSpawnedGroups.end(), group),
            mSpawnedGroups.end());
    };
    hooks.cameraPosition = [this]() { return mPlayer.eyePosition(); };
    hooks.cameraForward = [this]() { return mPlayer.forward(); };
    hooks.elapsed = [&engine]() { return engine.gameClock().elapsed(); };
    hooks.setTimeScale = [&engine](float scale) {
        engine.gameClock().setScale(scale);
    };
    hooks.timeScale = [&engine]() { return engine.gameClock().scale(); };
    mScripts->bindRuntime(hooks);

    // Inside the project's own working directory, so two games on one machine
    // cannot overwrite each other's saves. A bare .map has no project, and
    // saves beside the engine's other generated state.
    const std::filesystem::path saveDir =
        mProject.dir.empty() ? assets::project() / "artifacts"
                             : mProject.workDir();
    mScripts->bindSave((saveDir / "save.txt").string());

    script::registerScriptCommands(devConsole(), *mScripts);
}

// What the camera is, decided from the scene just built.
//
// Called after EVERY build, not only the first: a switch from a level to a shot
// changes the answer, and before this ran on a switch the player controller
// kept driving through an authored camera, and a screen rig outlived the page
// it was fitted for.
void ProjectApp::adoptSceneCamera(Engine& engine)
{
    Renderer& r = engine.renderer();

    // A screen rig belongs to the scene that asked for one. Dropped first, so
    // leaving a page for a world gives the camera back rather than presenting
    // a rig fitted to a scene that is gone.
    if (mScreen) {
        mScreen->detach(r);
        mScreen.reset();
        mWorld.setDrivesCamera(true);
    }

    // A scene that authored its own camera is a *shot*, not a level: it plays
    // itself, and the player controller would fight it for the renderer's one
    // camera every frame. So the controller stands down and the mouse stays
    // free -- which is also what makes such a scene recordable with --record
    // without a hand on the mouse.
    mCinematic = mScene && mScene->hasAuthoredCamera();
    if (mCinematic)
        log::info("Scene: authored camera -- playing as a shot");

    // A scene carrying a ScreenCamera is not a world at all but a 2D screen --
    // a menu, a HUD plate, a dialogue page -- and the camera belongs to the rig
    // that fits the page rather than to the authored entity transform.
    if (mScene) {
        if (const auto& screen = mScene->rig().screen) {
            mScreen.emplace();
            mScreen->setPage(*screen);
            mWorld.setDrivesCamera(false);
            mScreen->attach(r);
            mCinematic = true;
            log::info("Scene: screen scene -- %.0fx%.0f virtual pixels",
                      double(screen->pageWidth), double(screen->pageHeight));
        }
    }
    engine.input().setMouseGrab(!mCinematic);
}

bool ProjectApp::onStartGame(Engine& engine)
{
    const std::string path = scenePath();
    if (path.empty()) {
        exitCode = 1;
        return false;
    }

    Renderer& r = engine.renderer();
    mBackend.emplace(r);
    mWorld.attachRenderer(*mBackend);
    mWorld.attachPhysics(physics());
    mWorld.attachAudio(audio(), /*drivesListener=*/true);
    onWorldAttached(engine);

    mSceneGroup = nextGroup();
    if (!buildScene(engine, path)) {
        exitCode = 1;
        return false;
    }
    SceneRuntime& scene = *mScene;

    mPlayer.init(r, physics(), applyPlayFromOverride(playerStart(scene)), 6.0f,
                 0.0025f, glm::vec3(-500.0f), glm::vec3(500.0f));

    adoptSceneCamera(engine);

    startScripts(engine);

    if (mRecording)
        engine.startRecording(*mRecording);
    return true;
}

glm::vec3 ProjectApp::playerStart(const SceneRuntime& scene) const
{
    return scene.playerSpawn();
}

// Exactly one physics.update per frame, after the controller has posted its
// velocity, then one world sync to push the results at the renderer and the
// bodies.
//
// The script callbacks bracket that step, and the order is the contract:
//
//   fixed_update              immediately before the step it influences
//   Physics::update
//   on_collision/on_trigger   right after, so a script reacts to a hit in the
//                             same frame it happened
//   update                    with the rest of presentation
//   tickComponentSystems      then sync, so everything a script wrote this
//   World::sync               frame is what gets pushed at the renderer
//
// This mode has no fixed loop -- it steps physics from here -- which is exactly
// why fixedTick is defined as "before a physics step" rather than "on the fixed
// clock".
void ProjectApp::onPresent(const FrameContext& f)
{
    // A scene switch requested by a script last frame. First thing in the
    // frame, so nothing else this frame touches the world that is about to be
    // replaced.
    applyPendingScene(f.engine);

    Renderer& r = f.engine.renderer();
    if (!mCinematic)
        mPlayer.update(f.engine.input(), r, f.dt);

    if (mScripts)
        mScripts->fixedTick(f.dt);
    physics().update(f.dt);
    if (mScripts) {
        mScripts->drainContacts();
        mScripts->pollReload();
        mScripts->tick(f.dt);
    }

    // Geometry for anything a script spawned this frame. Before the sync that
    // materialises its node, so an entity built from Lua is visible on the
    // frame it appears rather than the one after -- and so `world.spawn` plus
    // an added PrimitiveMesh is a complete way to make a thing, which is what
    // makes spawning useful at all from a script.
    if (mScene) {
        mScene->resolveNewPrimitives(r);
        mScene->resolveMeshes(
            [this, &r](const std::string& mesh) {
                return loadSceneMesh(r, mesh);
            },
            /*onlyUnresolved=*/true);
    }

    // Component-driven motion -- spin, light animation, lifetimes -- before the
    // sync that pushes it. This is what makes an authored scene move without a
    // line of C++ per scene.
    ecs::tickComponentSystems(mWorld, f.dt);
    mWorld.sync();

    if (mScreen) {
        // The page is fitted every frame rather than once: a window resize
        // changes the aspect, and with Fit::Contain that changes how far back
        // the camera has to stand.
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.y > 0.0f)
            mScreen->setViewportAspect(display.x / display.y);
        mScreen->present(r, CameraPose{}, f.realDt);
    } else if (!mCinematic) {
        mPlayer.present(r, 1.0f, f.realDt);
    }
}

void ProjectApp::onStopGame(Engine& engine)
{
    onWorldDetaching(engine);
    // Before the world and physics go: the host holds a contact subscription
    // on one and an on_destroy hook on the other.
    mScripts.reset();
    mScene.reset();
    // Nodes and bodies die before the renderer and the physics world do.
    mWorld.detachAll();
    mBackend.reset();
}

} // namespace eng::runtime
