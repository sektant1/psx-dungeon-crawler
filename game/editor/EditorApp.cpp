#include "EditorApp.h"

#include "ComponentInspector.h"
#include "OutlinerPanel.h"
#include "PaintSlot.h"
#include "Picker.h"
#include "PickTarget.h"
#include "ViewportGrid.h"

#include <optional>
#include "SceneCook.h"

#include <algorithm>
#include "SceneSource.h"
#include "SceneTemplates.h"
#include "SceneValidate.h"
#include "SceneWriter.h"

#include <eng/Input.h>
#include <eng/Log.h>
#include <eng/render/Warmup.h>
#include <eng/render/ImGuiHint.h>
#include <eng/Renderer.h>
#include <eng/assets/AssetRoot.h>

#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_internal.h> // DockBuilder: the default panel layout

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace ed {
namespace {

using namespace game::content;

// What the editor opens when the command line names no .scn. Held as a bare
// filename because configure() -- which needs it for the loading hint -- runs
// before the resolver is mounted and cannot turn it into a path yet.
// The scene the editor opens with no file named. A small authored *shot* --
// one spinning prop, a portal behind it, a camera on an orbiting pivot --
// rather than a level: it opens in under a second, every component the editor
// can author is visible in one screen, and it is the thing to copy when making
// a clip. start_hall.scn is still in assets/scenes and still opens with
// `make editor SCENE=...`; it is a 128-entity level and was never a good first
// thing to look at.
constexpr const char* kDefaultScene = "spin_portal.scn";

// Grid drawn as world-space debug lines rather than an ImGui overlay: it has to
// sit *under* the geometry and take perspective, which a 2D draw list cannot
// do.
constexpr int kGridRadius = 16; // cells drawn either side of the camera

// How far out the placement ghost sits when the cursor points above the
// horizon, in metres. Roughly a room away: near enough to aim, far enough that
// the piece does not sit in the camera's face.
constexpr float kGhostFallbackDistance = 12.0f;

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoCollapse;

// The one place the editor decides how wide its view is. Walk mode deliberately
// uses the game's field of view rather than the editor's, because framing the
// same amount of world the player will see is the entire point of that mode --
// and every consumer has to agree on it, or the picker builds rays for a
// frustum the viewport is not drawing.
float viewportFovDeg(const EditorCamera& camera)
{
    return camera.activeFovDeg();
}

// The world box of a local one, under a resolved transform.
//
// It takes a WorldTransform rather than the authored one because a parented
// entity's authored transform is local: framing and picking both have to agree
// with what the viewport draws, and the viewport draws the cooked chain.
void transformedBounds(const WorldTransform& transform, const glm::vec3& localMin,
                       const glm::vec3& localMax, glm::vec3& worldMin,
                       glm::vec3& worldMax)
{
    const glm::mat4 matrix =
        glm::translate(glm::mat4(1.0f), transform.position) *
        glm::mat4_cast(transform.orientation) *
        glm::scale(glm::mat4(1.0f), transform.scale);
    worldMin = glm::vec3(1e9f);
    worldMax = glm::vec3(-1e9f);
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 local{
            (corner & 1) ? localMax.x : localMin.x,
            (corner & 2) ? localMax.y : localMin.y,
            (corner & 4) ? localMax.z : localMin.z,
        };
        const glm::vec3 world = glm::vec3(matrix * glm::vec4(local, 1.0f));
        worldMin = glm::min(worldMin, world);
        worldMax = glm::max(worldMax, world);
    }
}

glm::quat orientationFromMatrix(const glm::mat4& matrix, const glm::vec3& scale)
{
    glm::mat3 rotation(matrix);
    for (int axis = 0; axis < 3; ++axis) {
        const float divisor =
            std::abs(scale[axis]) > 1e-6f ? scale[axis] : 1.0f;
        rotation[axis] /= divisor;
    }
    return glm::normalize(glm::quat_cast(rotation));
}

} // namespace

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

// Nothing here may touch eng::assets: configure() runs before Engine::init,
// which is what discovers the content root and mounts the set. Paths are
// settled in onLoad, the first hook that runs with a mounted resolver.
eng::AppConfig EditorApp::configure(int argc, char** argv)
{
    mExecutablePath = argc > 0 ? argv[0] : "scene_editor";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.size() > 4 && arg.substr(arg.size() - 4) == ".scn")
            mPendingScene = arg;
    }

    eng::AppConfig config;
    config.mountSet = "editor";
    config.configPath = "config/editor.toml";
    config.fixedDt = 0.0f; // nothing here is simulated
    config.imgui = true;
    config.loadingTitle = "SCENE EDITOR";
    // Basename only: the full path is wider than the loading screen and the
    // part that identifies the scene is the tail.
    config.loadingHint =
        mPendingScene.empty()
            ? std::string(kDefaultScene)
            : mPendingScene.substr(mPendingScene.find_last_of('/') + 1);
    return config;
}

// The editor's slow startup is asset loading, not UI wiring: the kit catalog,
// the shared particle library and then every material the preview will ever
// show. All of it runs as load steps so the window shows the progress ring
// instead of a frozen grey rectangle.
void EditorApp::onLoad(eng::Engine& engine, eng::LoadPlan& plan)
{
    // The game pack's own directory. The editor is the one app that needs a
    // *directory* rather than a file: it saves new scenes and cooks maps into
    // the tree, and validate() checks a prefab's mesh against a root. Asking
    // for the pack by name keeps that honest -- and keeps working after the
    // tree moves, which APP_ASSET_DIR did not.
    mState.assetRoot = eng::assets::packDir("game").string();
    mState.kitPath = eng::assets::resolve("config/kit.toml").string();
    if (mPendingScene.empty())
        mPendingScene =
            eng::assets::resolve(std::string("scenes/") + kDefaultScene)
                .string();

    plan.add("Reading the kit catalog", [this] {
        std::string error;
        if (!KitCatalog::load(mState.kitPath, mState.catalog, error)) {
            eng::log::error("editor: %s", error.c_str());
            exitCode = 1;
            mCatalogFailed = true;
            return;
        }
        mState.grid = GridConfig::fromCatalog(mState.catalog);
    });
    // The editor tunes the same effects the game ships, so it loads the game's
    // own particles.toml rather than a copy that could drift out of sync.
    plan.add("Loading particle effects", [this, &engine] {
        mParticles.load(engine.renderer(),
                        eng::assets::resolve("config/particles.toml").string());
    });
    // The material panel renders a thumbnail per material on demand; compiling
    // them up front is what stops the first hover from stalling the editor.
    eng::addRenderWarmup(plan);
}

bool EditorApp::onStart(eng::Engine& engine)
{
    if (mCatalogFailed)
        return false;

    mEngine = &engine;
    eng::Renderer& renderer = engine.renderer();

    // Before anything draws: the profile and the view toggles the author left
    // set last session are what they expect to open into.
    applySettings();

    if (std::getenv("PSX_EDITOR_SELFTEST"))
        mSelfTestStep = 0;

    installConsoleCommands();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Gizmo styling. The defaults are tuned for a bright viewport and a small
    // window; against this dark scene at working distances they read as thin
    // grey scratches, and the handles are hard to hit.
    ImGuizmo::Style& gizmo = ImGuizmo::GetStyle();
    gizmo.TranslationLineThickness = 4.0f;
    gizmo.TranslationLineArrowSize = 8.0f;
    gizmo.RotationLineThickness = 3.0f;
    gizmo.RotationOuterLineThickness = 4.0f;
    gizmo.ScaleLineThickness = 4.0f;
    gizmo.ScaleLineCircleSize = 7.0f;
    gizmo.CenterCircleSize = 7.0f;
    // Axis colours brightened and pushed toward the conventional red/green/blue
    // for X/Y/Z, so which handle is which is readable at a glance rather than
    // something to work out from the direction it points.
    gizmo.Colors[ImGuizmo::DIRECTION_X] = ImVec4(0.95f, 0.28f, 0.32f, 1.0f);
    gizmo.Colors[ImGuizmo::DIRECTION_Y] = ImVec4(0.42f, 0.90f, 0.36f, 1.0f);
    gizmo.Colors[ImGuizmo::DIRECTION_Z] = ImVec4(0.30f, 0.55f, 1.00f, 1.0f);
    gizmo.Colors[ImGuizmo::PLANE_X] = ImVec4(0.95f, 0.28f, 0.32f, 0.45f);
    gizmo.Colors[ImGuizmo::PLANE_Y] = ImVec4(0.42f, 0.90f, 0.36f, 0.45f);
    gizmo.Colors[ImGuizmo::PLANE_Z] = ImVec4(0.30f, 0.55f, 1.00f, 0.45f);
    gizmo.Colors[ImGuizmo::SELECTION] = ImVec4(1.00f, 0.80f, 0.25f, 0.85f);
    gizmo.Colors[ImGuizmo::TEXT] = ImVec4(0.96f, 0.97f, 1.00f, 1.0f);
    gizmo.Colors[ImGuizmo::TEXT_SHADOW] = ImVec4(0.0f, 0.0f, 0.0f, 0.9f);
    // A constant slice of the viewport, so the handles stay the same size to
    // grab whether the selection is at arm's length or across the level.
    ImGuizmo::SetGizmoSizeClipSpace(0.14f);

    // The scene is rendered into a texture and shown inside a panel, so the
    // main window's own camera never draws anything the user sees.
    renderer.enableEditorViewport(1280, 720);
    applySceneEnvironment(renderer);

    // A key light so the preview reads as geometry rather than silhouettes.
    // This is editor lighting, not the scene's: the game applies its own.
    eng::LightDesc key;
    key.type = eng::LightDesc::Type::Directional;
    key.colour = {0.95f, 0.93f, 0.88f};
    eng::NodeHandle keyNode =
        renderer.createNode(eng::kRootNode, {0.0f, 12.0f, 0.0f});
    renderer.setOrientation(
        keyNode,
        glm::angleAxis(glm::radians(-55.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    renderer.attachLight(keyNode, key);

    mPreview = std::make_unique<PreviewBridge>(renderer);
    mState.camera.setFlyPosition({0.0f, 14.0f, 26.0f});
    mState.camera.setYawPitch(0.0f, -0.45f);

    // Session state, not project content: it is per-machine, so it lives beside
    // the other generated artefacts rather than in the asset tree.
    mRecentFile = (eng::assets::project() / "artifacts" / "editor" / "recent.txt")
                      .string();
    mRecent.load(mRecentFile);
    mSettingsFile =
        (eng::assets::project() / "artifacts" / "editor" / "settings.txt")
            .string();
    mSettings = loadEditorSettings(mSettingsFile);
    mAutosaveIn = mSettings.autosaveSeconds;

    // The ids the game defines, so a spawn is picked from a list rather than
    // spelled from memory. Resolved through the mount list like any other
    // content; a missing file leaves the field free text.
    if (const std::filesystem::path enemies =
            eng::assets::resolve("config/enemies.toml");
        !enemies.empty()) {
        mEnemyIds = enemyIdsFromToml(enemies.string());
        eng::log::info("Editor: %zu enemy ids", mEnemyIds.size());
    }
    if (const std::filesystem::path pickups =
            eng::assets::resolve("config/prototypes.toml");
        !pickups.empty())
        mPickupIds = tomlSectionIds(pickups.string(), "pickup");
    if (const std::filesystem::path palettes =
            eng::assets::resolve("config/palettes.toml");
        !palettes.empty()) {
        mPalettesPath = palettes.string();
        mPalettes = tomlSectionIds(mPalettesPath, "palette");
        eng::log::info("Editor: %zu palettes", mPalettes.size());
    }

    if (!mPendingScene.empty() && loadScene(mPendingScene)) {
        mRecent.touch(mPendingScene);
        mRecent.save(mRecentFile);
        if (autosaveIsStale(mPendingScene, mState.assetRoot + "/scenes"))
            mStatus += "  |  a newer autosave exists -- Scene > Recover "
                       "autosave";
    }
    // Verification hook: start in the staging scene so a screenshot run can
    // capture it without driving the UI.
    if (std::getenv("PSX_EDITOR_MATERIAL"))
        setMode(true);
    // Verification hook: stage one material by name. Without it a screenshot
    // run can only cycle the whole list and hope, which cannot show a specific
    // rig (the quad is only reachable through a handful of names).
    if (const char* pick = std::getenv("PSX_EDITOR_MATERIAL_NAME")) {
        mSelectedMaterial = pick;
        if (mStage.built())
            mStage.setMaterial(engine.renderer(), mSelectedMaterial);
    }
    // Verification hooks: drive the two interactions a screenshot run cannot
    // click on its own.
    if (std::getenv("PSX_EDITOR_CYCLE_MATERIALS"))
        mCycleMaterials = true;
    // Verification hook: the surfaces that only exist while a key is held or a
    // menu is open, and that a screenshot run therefore cannot reach.
    if (const char* panel = std::getenv("PSX_EDITOR_PANEL")) {
        const std::string which = panel;
        if (which == "palette")
            openPalette(mPalette);
        else if (which == "open")
            mOpenSceneOpen = true;
        else if (which == "help")
            mHelpOpen = true;
        else
            // Any other value names a docked panel to bring forward. Panels
            // share tabs, so a screenshot run cannot otherwise reach the one it
            // means to photograph.
            mFocusPanel = which;
    }
    if (const char* query = std::getenv("PSX_EDITOR_PALETTE_QUERY")) {
        openPalette(mPalette);
        std::snprintf(mPalette.query, sizeof(mPalette.query), "%s", query);
    }
    // Verification hook: build a room without a mouse, so a screenshot run can
    // show what the tool produces.
    if (const char* room = std::getenv("PSX_EDITOR_DEMO_ROOM")) {
        mState.document = SceneDocument{};
        mState.document.id = "scene.demo_room";
        int w = 4, d = 3;
        std::sscanf(room, "%dx%d", &w, &d);
        RoomSpec spec = mState.roomSpec;
        spec.col0 = 0;
        spec.row0 = 0;
        spec.col1 = w - 1;
        spec.row1 = d - 1;
        std::string error;
        for (const Entity& piece : buildRoom(mState.grid, mState.catalog, spec,
                                             mState.document, error))
            mState.document.add(piece);
        mState.document.touch();
        mPreview->invalidate();
        glm::vec3 min, max;
        if (boundsOf({}, min, max))
            frameCamera(min, max);
        mStatus = error.empty() ? "demo room built" : error;
    }
    if (const char* select = std::getenv("PSX_EDITOR_SELECT")) {
        // An id if the document has one, otherwise an index -- a screenshot run
        // that wants "the arena gate" should not have to count entities.
        if (mState.document.contains(select))
            selectAndReveal(select, false);
        else if (!mState.document.entities.empty()) {
            const std::size_t index = std::size_t(std::atoi(select)) %
                                      mState.document.entities.size();
            selectAndReveal(mState.document.entities[index].id, false);
        }
        // Framed as well as selected: a capture that names an entity wants to
        // see it, and the alternative is guessing camera coordinates.
        frameSelectionOrAll();
    }
    // Verification hook: start at the player's eye, at the spawn. The only
    // view that answers a level-design question -- is the exit legible from
    // where the player wakes up -- is the one the player has.
    if (std::getenv("PSX_EDITOR_WALK"))
        toggleWalk();
    // Verification hook: arm the Place tool with a piece, so a capture can show
    // the placement ghost -- which otherwise needs a click in the Catalog.
    if (const char* brush = std::getenv("PSX_EDITOR_BRUSH")) {
        if (mState.catalog.find(brush)) {
            mState.brushPrefab = brush;
            mState.tool = Tool::Place;
        }
    }
    // Verification hook: give the selection a component without the mouse. Runs
    // the same path the inspector's Add Component does, so a capture shows what
    // an author would get.
    if (const char* add = std::getenv("PSX_EDITOR_ADD_COMPONENT"))
        if (const ComponentType* type = findComponentType(add))
            addComponentToSelection(*type);

    return true;
}

bool EditorApp::loadScene(const std::string& path)
{
    std::string error;
    SceneDocument document;
    if (!loadSceneSource(path, document, error)) {
        mStatus = error;
        eng::log::error("editor: %s", error.c_str());
        return false;
    }
    mState.document = std::move(document);
    mState.document.touch();
    mState.scenePath = path;
    mState.dirty = false;
    mState.selection.clear();
    if (mPreview)
        mPreview->invalidate();

    const std::vector<Issue> issues =
        validate(mState.document, mState.catalog, mState.assetRoot);
    glm::vec3 min, max;
    if (boundsOf({}, min, max))
        frameCamera(min, max);

    mStatus = "opened " + std::filesystem::path(path).filename().string() +
              " -- " + std::to_string(mState.document.entities.size()) +
              " entities, " + std::to_string(issues.size()) + " issues";
    return true;
}

bool EditorApp::boundsOf(const std::vector<AuthorId>& ids, glm::vec3& min,
                         glm::vec3& max) const
{
    bool any = false;
    min = glm::vec3(1e9f);
    max = glm::vec3(-1e9f);
    const auto include = [&](const Entity& entity) {
        glm::vec3 localMin(-0.5f), localMax(0.5f); // a marker is a small cube
        if (const KitPiece* piece = mState.catalog.find(entity.prefab))
            piece->localBoundsMeters(mState.catalog.scale(), localMin,
                                     localMax);
        glm::vec3 entityMin, entityMax;
        transformedBounds(mState.document.worldTransform(entity.id), localMin,
                          localMax, entityMin, entityMax);
        min = glm::min(min, entityMin);
        max = glm::max(max, entityMax);
        any = true;
    };
    if (ids.empty()) {
        for (const Entity& entity : mState.document.entities)
            include(entity);
    }
    else {
        // Descendants count. Selecting a chandelier and pressing F used to
        // frame the hub and leave the four candles outside the view, and the
        // selection outline drew a box a third the size of the thing the gizmo
        // was about to move -- so the outline actively lied about its subject.
        // Every other verb here (delete, duplicate, copy, hide, lock) already
        // takes descendants; this was the one that did not.
        for (const AuthorId& id : withDescendants(mState.document, ids))
            if (const Entity* entity = mState.document.find(id))
                include(*entity);
    }
    return any;
}

// F, and the toolbar button: frame the selection, or the whole scene when
// nothing is selected. Framing nothing would leave the camera staring into
// empty space, which reads as the editor having lost the level.
void EditorApp::frameSelectionOrAll()
{
    glm::vec3 min, max;
    if (boundsOf(mState.selection, min, max) || boundsOf({}, min, max))
        frameCamera(min, max);
}

void EditorApp::frameCamera(const glm::vec3& min, const glm::vec3& max)
{
    const glm::vec3 centre = (min + max) * 0.5f;
    const float radius = glm::max(glm::length(max - min) * 0.5f, 2.0f);
    // Pull back far enough that the whole extent fits the vertical fov, then a
    // little more so it does not touch the panel edges.
    const float distance =
        radius / std::tan(glm::radians(EditorCamera::kEditorFovDeg * 0.5f)) *
        1.25f;
    // Steep, because a dungeon is a closed box: framed from a shallow angle the
    // camera just stares at the outside of the nearest wall. Looking down into
    // it (the levels have no ceilings authored) is what actually shows the
    // level, and it is where every level editor's default view sits.
    const float pitch = -1.15f;
    const glm::vec3 back{0.0f, -std::sin(pitch), std::cos(pitch)};
    mState.camera.setYawPitch(0.0f, pitch);
    mState.camera.setFlyPosition(centre + back * distance);
    mState.camera.frame(centre, distance);
}

void EditorApp::runCommand(Command command)
{
    mCommands.run(mState.document, std::move(command));
    mState.dirty = !mCommands.savedStateReached();
    // Any edit invalidates the cooked map; saying so beats letting someone
    // playtest a map that predates the change they are looking at.
    mCookStatus = "stale";
}

void EditorApp::applyHistory(bool redo)
{
    const bool moved = redo ? mCommands.redo(mState.document)
                            : mCommands.undo(mState.document);
    if (!moved)
        return;
    mState.document.touch();
    mPreview->invalidate();
    mState.dirty = !mCommands.savedStateReached();
    mCookStatus = "stale";
}

// A fresh document from a template. Templates always include a spawn and an
// exit, so a new scene cooks and plays from the first frame -- handing someone
// a document that refuses to run is a bad first thirty seconds.
void EditorApp::newScene(SceneTemplate which)
{
    std::string error;
    SceneDocument document;
    if (!buildTemplate(which, mState.grid, mState.catalog,
                       std::string("scene.") + (which == SceneTemplate::TechDemo
                                                    ? "tech_demo"
                                                    : "untitled"),
                       document, error)) {
        mStatus = error;
        return;
    }
    mState.document = std::move(document);
    mState.document.touch();
    mState.scenePath.clear(); // Save As, not Save: this has no file yet
    mState.selection.clear();
    mCommands.clear();
    mState.dirty = true;
    mCookStatus = "not cooked";
    mPreview->invalidate();
    glm::vec3 min, max;
    if (boundsOf({}, min, max))
        frameCamera(min, max);
    mStatus = std::string("new scene from the '") + sceneTemplateName(which) +
              "' template -- Save as... to give it a file";
}

// --- discarding the open document --------------------------------------------

void EditorApp::requestDiscard(Discard what, SceneTemplate which)
{
    mDiscardWhat = what;
    mDiscardTemplate = which;
    // A clean document has nothing to lose, so the prompt would be pure
    // ceremony. Only unsaved work earns an interruption.
    if (!mState.dirty) {
        performDiscard();
        return;
    }
    mDiscardOpen = true;
}

void EditorApp::performDiscard()
{
    mDiscardOpen = false;
    switch (mDiscardWhat) {
    case Discard::Quit:
        if (mEngine)
            mEngine->requestClose();
        break;
    case Discard::Reload:
        if (!mState.scenePath.empty())
            loadScene(mState.scenePath);
        break;
    case Discard::NewScene:
        newScene(mDiscardTemplate);
        break;
    case Discard::Open:
        if (!mPendingOpen.empty()) {
            const std::string path = mPendingOpen;
            mPendingOpen.clear();
            if (loadScene(path)) {
                mCommands.clear();
                mCookStatus = "not cooked";
                mAutosaveOffered = false;
                // A recovered backup is not a scene you meant to open: it is
                // one you meant to *rescue*, and it stops existing the moment
                // it is saved over the real file.
                if (!isAutosavePath(path)) {
                    mRecent.touch(path);
                    mRecent.save(mRecentFile);
                }
                if (autosaveIsStale(path, mState.assetRoot + "/scenes"))
                    mStatus += "  |  a newer autosave exists -- Scene > "
                               "Recover autosave";
            }
            else {
                // A scene that will not open is usually one that moved. Keep
                // the list honest rather than offering the same dead row again.
                mRecent.remove(path);
                mRecent.save(mRecentFile);
            }
        }
        break;
    }
}

void EditorApp::requestOpen(const std::string& path)
{
    if (path.empty())
        return;
    mPendingOpen = path;
    mOpenSceneOpen = false;
    requestDiscard(Discard::Open);
}

void EditorApp::drawDiscardPopup()
{
    static constexpr const char* kTitle = "Unsaved changes";
    if (mDiscardOpen && !ImGui::IsPopupOpen(kTitle))
        ImGui::OpenPopup(kTitle);
    if (!ImGui::BeginPopupModal(kTitle, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const char* verb = mDiscardWhat == Discard::Quit     ? "Quitting"
                       : mDiscardWhat == Discard::Reload ? "Reloading"
                       : mDiscardWhat == Discard::Open
                           ? "Opening another scene"
                           : "Starting a new scene";
    ImGui::Text("%s will discard unsaved changes to", verb);
    ImGui::TextUnformatted(mState.scenePath.empty() ? "this untitled scene."
                                                    : mState.scenePath.c_str());
    ImGui::Separator();

    // Save is the default: it is the only choice here that cannot lose work, so
    // it takes Enter and the leftmost position.
    if (ImGui::Button("Save and continue") ||
        ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        // An untitled scene has nowhere to save to, so the discard waits while
        // Save As collects a path rather than silently failing.
        if (mState.scenePath.empty()) {
            mDiscardOpen = false;
            mSaveAsOpen = true;
            ImGui::CloseCurrentPopup();
        }
        else if (saveScene()) {
            ImGui::CloseCurrentPopup();
            performDiscard();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
        ImGui::CloseCurrentPopup();
        performDiscard();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        mDiscardOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

bool EditorApp::saveScene()
{
    if (mState.scenePath.empty())
        return false;
    std::string error;
    if (!writeSceneSource(mState.scenePath, mState.document, error)) {
        mStatus = error;
        return false;
    }
    mCommands.markSaved();
    mState.dirty = false;
    mStatus =
        "saved " + std::filesystem::path(mState.scenePath).filename().string();
    return true;
}

// The cook the CLI does, called in-process. Same function, so the bytes match
// what CI produces -- the cook_parity test exists to keep that true.
bool EditorApp::cookScene(std::string& mapPath)
{
    const std::vector<Issue> issues =
        validate(mState.document, mState.catalog, mState.assetRoot);
    if (blocksCook(issues)) {
        mStatus = "cook refused: fix the blocking issues first";
        mCookStatus = "blocked";
        return false;
    }
    std::filesystem::path target(mState.scenePath.empty()
                                     ? mState.assetRoot + "/scenes/untitled.scn"
                                     : mState.scenePath);
    target.replace_extension(".map");
    mapPath = target.string();

    std::string error;
    if (!cookToMap(mState.document, mState.catalog, mapPath, error)) {
        mStatus = error;
        mCookStatus = "failed";
        return false;
    }
    mCookStatus = "fresh";
    mStatus = "cooked " + std::filesystem::path(mapPath).filename().string();
    return true;
}

void EditorApp::runPlaytest()
{
    if (mPlaytest.running()) {
        stopGame(mPlaytest);
        mStatus = "playtest stopped";
        return;
    }
    // An untitled scene has nowhere to save to, and saveScene() reports that by
    // returning false with nothing said -- so F5 on a fresh scene used to do
    // absolutely nothing, with no message explaining why. Ask for a path.
    if (mState.dirty && mState.scenePath.empty()) {
        mSaveAsOpen = true;
        std::snprintf(mSaveAsPath, sizeof(mSaveAsPath), "%s",
                      (mState.assetRoot + "/scenes/untitled.scn").c_str());
        mStatus = "name the scene before playtesting it";
        return;
    }
    if (mState.dirty && !saveScene())
        return;
    std::string mapPath;
    if (!cookScene(mapPath))
        return;

    const std::string exe = siblingExecutable(mExecutablePath, "game");
    // Under artifacts/, with every other generated file, rather than dropped in
    // the project root. Asked for by name -- this used to climb "/../.." out of
    // an asset path, which only worked because of where the tree happened to
    // sit. The directory is created because a playtest may be the first thing
    // a fresh clone does.
    const std::filesystem::path logDir = eng::assets::project() / "artifacts";
    std::error_code logDirEc;
    std::filesystem::create_directories(logDir, logDirEc);
    const std::string log = (logDir / "playtest.log").string();

    // Start where the author is looking, unless they asked for the spawn.
    // Adjusting a room at the far end of a level and then walking to it from
    // the entrance on every iteration is what makes people stop playtesting --
    // which is the same as not checking the work.
    std::string playFrom;
    if (mPlayFromCamera && !mState.camera.walking()) {
        const glm::vec3 eye = mState.camera.activeEye();
        // Raised a little: the editor camera is often just inside geometry, and
        // a character spawned inside a wall is pushed somewhere unhelpful.
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "%.3f,%.3f,%.3f", double(eye.x),
                      double(eye.y) + 0.5, double(eye.z));
        playFrom = buffer;
    }
    // Everything the playtest starts with, in one place. Each field is an
    // environment variable the game already reads, so the editor is choosing
    // between the game's own options rather than growing a second set.
    PlaytestEnvironment options;
    options.playFrom = playFrom;
    options.renderPreset = playtestPresetName();
    options.console = mSettings.playtestConsole;
    options.colliders = mSettings.playtestColliders;
    options.fullscreen = mSettings.playtestFullscreen;
    mPlaytest = launchGame(exe, mapPath, log, playtestEnvironment(options));
    if (!mPlaytest.running()) {
        mStatus = mPlaytest.error;
        return;
    }
    mStatus = "playtest running (pid " + std::to_string(mPlaytest.pid) + ")";
}

void EditorApp::toggleWalk()
{
    if (mState.camera.walking()) {
        mState.camera.leaveWalk();
        mStatus = "back to the editor camera";
        return;
    }

    // Start where the player will: the spawn, or failing that whatever is
    // selected, so a room with no spawn yet can still be eyeballed.
    const Entity* from = nullptr;
    for (const Entity& entity : mState.document.entities) {
        if (entity.playerSpawn) {
            from = &entity;
            break;
        }
    }
    if (!from && !mState.selection.empty())
        from = mState.document.find(*mState.primary());
    if (!from) {
        mStatus = "walk needs a player spawn or a selection to stand at";
        return;
    }

    mState.camera.enterWalk(from->transform.position,
                            from->transform.rotationDegrees.y);
    mStatus = "walk preview -- hold right mouse to look, WASD to move";
}

void EditorApp::deleteSelection()
{
    if (mState.selection.empty())
        return;
    // Descendants go with their parent. Leaving them behind would turn every
    // delete of a composed object into a scatter of orphans pointing at an
    // entity that no longer exists -- valid enough to open, wrong everywhere.
    const std::vector<AuthorId> doomed =
        withDescendants(mState.document, mState.selection);

    // A large delete is confirmed, a small one is not. Undo covers both, but a
    // stray Del over a selected room is the one edit an author does not notice
    // happening -- forty pieces vanish and the next twenty actions bury it in
    // the history. One click is cheaper than that.
    if (doomed.size() >= kConfirmDeleteAbove) {
        const std::size_t extra = doomed.size() - mState.selection.size();
        ConfirmDialog::open(
            "Delete " + std::to_string(doomed.size()) + " entities?",
            extra > 0 ? std::to_string(extra) +
                            " of them are parented to the selection"
                      : std::string("This can be undone with Ctrl+Z."),
            [this, doomed] { commitDelete(doomed); });
        return;
    }
    commitDelete(doomed);
}

void EditorApp::commitDelete(const std::vector<AuthorId>& ids)
{
    std::vector<Command> parts;
    for (const AuthorId& id : ids) {
        if (mState.document.contains(id))
            parts.push_back(makeDeleteEntity(mState.document, id));
    }
    if (parts.empty())
        return;
    runCommand(
        makeComposite("delete " + std::to_string(parts.size()) + " entities",
                      std::move(parts)));
    mState.selection.clear();
    mPreview->invalidate();
}

// Adds `copies` as one undo entry and selects them. Shared by duplicate and
// paste, which differ only in where the entities came from.
void EditorApp::addCopies(const std::vector<Entity>& copies,
                          const std::string& label)
{
    if (copies.empty())
        return;
    std::vector<Command> parts;
    std::vector<AuthorId> fresh;
    parts.reserve(copies.size());
    for (const Entity& copy : copies) {
        fresh.push_back(copy.id);
        parts.push_back(makeCreateEntity(copy));
    }
    runCommand(makeComposite(label + " " + std::to_string(copies.size()) +
                                 (copies.size() == 1 ? " entity" : " entities"),
                             std::move(parts)));
    // Selecting the copies is what makes the next drag act on the new thing,
    // which is the only reason anyone duplicates.
    mState.selection = fresh;
    if (!fresh.empty()) {
        mSelectionAnchor = fresh.front();
        mOutlinerReveal = fresh.front();
    }
    mPreview->invalidate();
}

void EditorApp::duplicateSelection()
{
    if (mState.selection.empty())
        return;
    // Descendants come along, and offsetCopies re-points the links between the
    // copies: duplicating a chandelier gives a second chandelier with its own
    // candles, not four candles hanging off the first one.
    const std::vector<Entity> source = collectEntities(
        mState.document, withDescendants(mState.document, mState.selection));
    addCopies(offsetCopies(mState.document, source, mState.grid.cell, 1),
              "duplicate");
}

void EditorApp::copySelection(bool cut)
{
    if (mState.selection.empty())
        return;
    mClipboard = collectEntities(
        mState.document, withDescendants(mState.document, mState.selection));
    mStatus = (cut ? "cut " : "copied ") + std::to_string(mClipboard.size()) +
              " entities";
    if (cut)
        deleteSelection();
}

void EditorApp::pasteClipboard()
{
    if (mClipboard.empty()) {
        mStatus = "nothing to paste";
        return;
    }
    // Offset from the originals, not from the cursor: a paste that lands under
    // the pointer is a different feature (place), and one that lands exactly on
    // the original is invisible.
    addCopies(offsetCopies(mState.document, mClipboard, mState.grid.cell, 1),
              "paste");
}

// --- autosave ----------------------------------------------------------------

void EditorApp::writeAutosave()
{
    const std::string path =
        autosavePath(mState.scenePath, mState.assetRoot + "/scenes");
    if (path.empty())
        return;
    std::string error;
    if (!writeSceneSource(path, mState.document, error)) {
        // Reported once, in the log rather than the status bar: a backup that
        // fails must not steal the line the author is reading, but a silent one
        // is worse than none.
        eng::log::warn("editor: autosave failed: %s", error.c_str());
        return;
    }
    eng::log::info("editor: autosaved to %s", path.c_str());
}

void EditorApp::tickAutosave(float dt)
{
    // The rule itself is a pure function in EditorSettings, so it is testable
    // without waiting two minutes with an editor open.
    const AutosaveTick tick =
        stepAutosave(mSettings, mAutosaveIn, dt, mState.dirty);
    mAutosaveIn = tick.remaining;
    if (tick.write)
        writeAutosave();
}

void EditorApp::runSelfTest(eng::Engine& engine)
{
    if (mSelfTestStep < 0)
        return;
    // One edit per frame, so each one is followed by a full frame of preview
    // rebuild, gizmo rebuild, outliner rebuild and picking -- which is where
    // these were crashing, not in the edit itself.
    const int step = mSelfTestStep++;
    const auto pick = [this](const char* wanted) -> AuthorId {
        for (const Entity& entity : mState.document.entities)
            if (entity.id.rfind(wanted, 0) == 0)
                return entity.id;
        return {};
    };
    const auto component = [](const char* id) {
        return findComponentType(id);
    };

    // Each edit is preceded by a frame that only selects, so the frame the
    // edit lands on has already drawn the gizmo, the inspector section and the
    // outliner row for that entity -- which is the state a hand-driven click
    // is always in, and the one the crash reports came from.
    struct Step {
        const char* entity;   // id prefix to select
        const char* action;   // "remove:<component>", "detach", "delete"
    };
    static const Step kSteps[] = {
        {"prop_raccoon", "remove:shader"},
        {"prop_raccoon", "remove:mesh"},
        {"prop_raccoon", "remove:spin"},
        {"camera_main", "detach"},
        {"camera_main", "remove:camera"},
        {"prop_base", "delete"},
        {"prop_crystal", "delete"},
        {"light_key", "remove:light"},
    };
    constexpr int kCount = int(sizeof(kSteps) / sizeof(kSteps[0]));

    if (step >= kCount * 2) {
        eng::log::info("selftest: survived");
        engine.requestClose();
        return;
    }
    const Step& current = kSteps[step / 2];
    const AuthorId id = pick(current.entity);
    if (id.empty())
        return;
    if (step % 2 == 0) { // the selecting frame
        selectAndReveal(id, false);
        return;
    }

    const std::string action = current.action;
    eng::log::info("selftest: %s on %s", action.c_str(), id.c_str());
    if (action == "detach") {
        detachSelection();
    } else if (action == "delete") {
        deleteSelection();
    } else if (action.rfind("remove:", 0) == 0) {
        if (const ComponentType* type = component(action.substr(7).c_str()))
            removeComponentFromSelection(*type);
    }
}

void EditorApp::applySettings()
{
    mGameLighting = mSettings.gameLighting;
    mShowEntityGizmos = mSettings.entityMarks;
    mShowGizmoVolumes = mSettings.volumeMarks;
    mShowFrameStats = mSettings.frameStats;
    mPlayFromCamera = mSettings.playFromCamera;

    if (!mEngine)
        return;
    if (!mSettings.viewportPreset.empty()) {
        const int id = eng::renderPresetFromName(mSettings.viewportPreset.c_str());
        if (id > 0) {
            mEngine->setRenderPreset(id);
        } else {
            // Named a profile that no longer exists: say so and forget it,
            // rather than reporting it again on every launch forever.
            eng::log::warn("editor: unknown render preset '%s' in settings",
                           mSettings.viewportPreset.c_str());
            mSettings.viewportPreset.clear();
        }
    }
    applySceneEnvironment(mEngine->renderer());
}

std::string EditorApp::playtestPresetName() const
{
    // Matching the viewport is the default because per-entity ShaderParams -- a
    // rim light, a tint, an opacity -- read completely differently under `ps1`
    // and under `dungeon`. Tuning one in the editor and playing under another
    // is tuning blind.
    if (mSettings.playtestMatchesViewport) {
        if (!mEngine)
            return mSettings.viewportPreset;
        return eng::renderPresetName(mEngine->renderPreset());
    }
    return mSettings.playtestPreset;
}

void EditorApp::captureSettings()
{
    // The other direction, once a frame: the View menu, the toolbar and the
    // render-preset submenu all edit the live state directly, and a preference
    // that only persists when it was changed from the settings panel is a
    // preference the author will find reset tomorrow. One rule instead of a
    // commit call on every widget that touches one of these.
    EditorSettings next = mSettings;
    next.gameLighting = mGameLighting;
    next.entityMarks = mShowEntityGizmos;
    next.volumeMarks = mShowGizmoVolumes;
    next.frameStats = mShowFrameStats;
    next.playFromCamera = mPlayFromCamera;
    if (mEngine)
        next.viewportPreset = eng::renderPresetName(mEngine->renderPreset());

    if (next.gameLighting == mSettings.gameLighting &&
        next.entityMarks == mSettings.entityMarks &&
        next.volumeMarks == mSettings.volumeMarks &&
        next.frameStats == mSettings.frameStats &&
        next.playFromCamera == mSettings.playFromCamera &&
        next.viewportPreset == mSettings.viewportPreset)
        return;
    mSettings = next;
    if (!saveEditorSettings(mSettingsFile, mSettings))
        eng::log::warn("editor: could not write %s", mSettingsFile.c_str());
}

void EditorApp::commitSettings()
{
    mSettings = sanitised(mSettings);
    // The countdown belongs to the old interval; re-arm it so a change takes
    // effect now rather than after one more backup at the previous rate.
    mAutosaveIn = std::min(mAutosaveIn, mSettings.autosaveSeconds);
    if (!saveEditorSettings(mSettingsFile, mSettings))
        eng::log::warn("editor: could not write %s", mSettingsFile.c_str());
}

void EditorApp::recoverAutosave()
{
    const std::string path =
        autosavePath(mState.scenePath, mState.assetRoot + "/scenes");
    if (path.empty() || !std::filesystem::exists(path)) {
        mStatus = "no autosave to recover";
        return;
    }
    // Recovery goes through the same door as any other open, so the work
    // currently on screen is not thrown away without the prompt. The recovered
    // document keeps the backup's path until Save as: writing it straight over
    // the scene would make the recovery itself unrecoverable.
    requestOpen(path);
}

void EditorApp::onFrameBegin(const eng::FrameContext& f)
{
    eng::Input& input = f.engine.input();

    // Right button held over the viewport = fly.
    //
    // Deliberately NOT gated on io.WantCaptureMouse: the viewport IS an ImGui
    // window, so ImGui always wants the mouse while the cursor is over it, and
    // testing that flag meant the fly camera could never engage anywhere it was
    // useful. mViewportHovered (ImGui::IsWindowHovered on the viewport panel)
    // is the question actually being asked -- is the cursor over the 3D view.
    const bool wantsFly = input.isMouseDown(eng::MouseButton::Right) &&
                          (mFlying || mViewportHovered);
    if (wantsFly != mFlying) {
        mFlying = wantsFly;
        input.setMouseGrab(mFlying);
    }

    if (mFlying) {
        const glm::vec2 delta = input.mouseDelta();
        constexpr float kLookSpeed = 0.0032f;
        // Walk mode borrows the same right-drag-to-look gesture, but drives the
        // walk camera: it has its own yaw/pitch so that peeking at eye level
        // cannot disturb the framing the author will come back to.
        if (mState.camera.walking()) {
            mState.camera.walkLook(-delta.x * kLookSpeed,
                                   -delta.y * kLookSpeed);
        }
        else {
            mState.camera.addYawPitch(-delta.x * kLookSpeed,
                                      -delta.y * kLookSpeed);
        }

        // WASD + QE, the movement every 3D editor since Quake has used.
        glm::vec3 move{0.0f};
        if (input.isDown("fly_forward"))
            move.z -= 1.0f;
        if (input.isDown("fly_back"))
            move.z += 1.0f;
        if (input.isDown("fly_left"))
            move.x -= 1.0f;
        if (input.isDown("fly_right"))
            move.x += 1.0f;
        if (input.isDown("fly_down"))
            move.y -= 1.0f;
        if (input.isDown("fly_up"))
            move.y += 1.0f;
        if (move != glm::vec3(0.0f)) {
            if (mState.camera.walking()) {
                // A walking pace, not a flying one, and Q/E do nothing: the
                // whole point is to see the level from where a player's head
                // will be, and a preview that drifts upward is not that.
                mState.camera.walkMove(
                    glm::normalize(move) *
                    (EditorCamera::kWalkSpeed *
                     (input.isDown("fly_fast") ? 2.5f : 1.0f) * f.dt));
            }
            else {
                const float speed =
                    (input.isDown("fly_fast") ? 36.0f : 12.0f) * f.dt;
                mState.camera.moveLocal(glm::normalize(move) * speed);
            }
        }
    }

    // Letter shortcuts stay mute while a text field owns the keyboard, so
    // typing a name into the inspector cannot fly the camera or delete a thing.
    // Nothing here fires while flying. editor.toml deliberately binds the tool
    // keys to the same letters as the fly controls (Q/W/E are both tools and
    // down/forward/up), so the whole block is modal, not just the three that
    // collide by name: WASD held down during a flight would otherwise trip
    // focus, rotate, grid steps -- and delete.
    if (!ImGui::GetIO().WantCaptureKeyboard && !mFlying) {
        if (input.wasPressed("focus"))
            frameSelectionOrAll();
        const bool interactionActive =
            mGizmoDragging || mPainting || mRoomDragging ||
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (!interactionActive && input.wasPressed("tool_select"))
            mState.tool = Tool::Select;
        if (!interactionActive && input.wasPressed("tool_place"))
            mState.tool = Tool::Place;
        if (!interactionActive && input.wasPressed("tool_room"))
            mState.tool = Tool::Room;
        if (input.wasPressed("grid_coarser"))
            mState.gridState.coarser();
        if (input.wasPressed("grid_finer"))
            mState.gridState.finer();
        if (input.wasPressed("toggle_snap"))
            mState.gridState.snap = !mState.gridState.snap;
        if (input.wasPressed("level_up"))
            mState.gridState.level += mState.grid.cell;
        if (input.wasPressed("level_down"))
            mState.gridState.level -= mState.grid.cell;
        if (input.wasPressed("level_reset"))
            mState.gridState.level = 0.0f;
        if (!interactionActive && input.wasPressed("gizmo_cycle"))
            mGizmoOperation = (mGizmoOperation + 1) % 3;
        // The Ctrl guards are what let X and V carry a second, modified
        // meaning: without them Ctrl+X flipped the gizmo frame on its way to
        // cutting, and Ctrl+V dropped the camera to the player's eye.
        if (!interactionActive && !ImGui::GetIO().KeyCtrl &&
            input.wasPressed("gizmo_space"))
            mGizmoLocal = !mGizmoLocal;
        if (!ImGui::GetIO().KeyCtrl && input.wasPressed("walk"))
            toggleWalk();
        if (!interactionActive && input.wasPressed("delete"))
            deleteSelection();

        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && input.wasPressed("save"))
            saveScene();
        if (input.wasPressed("run"))
            runPlaytest();
        if (input.wasPressed("cook")) {
            std::string mapPath;
            cookScene(mapPath);
        }
        if (io.KeyCtrl && input.wasPressed("duplicate"))
            duplicateSelection();
        if (io.KeyCtrl && input.wasPressed("undo"))
            applyHistory(io.KeyShift);
        if (io.KeyCtrl && input.wasPressed("redo"))
            applyHistory(true);
        if (io.KeyCtrl && input.wasPressed("open"))
            mOpenSceneOpen = true;
        if (io.KeyCtrl && input.wasPressed("copy"))
            copySelection(false);
        if (io.KeyCtrl && input.wasPressed("cut"))
            copySelection(true);
        if (io.KeyCtrl && input.wasPressed("paste"))
            pasteClipboard();
        if (input.wasPressed("help"))
            mHelpOpen = !mHelpOpen;
        if (input.wasPressed("dev_console"))
            mConsole.toggle();
    }

    // Escape cancels, and only quits when there is nothing left to cancel. It
    // is the key people press to back out of a mode, so making it close the
    // editor outright is how an afternoon of blockout gets thrown away by
    // reflex.
    if (input.wasPressed("quit")) {
        if (mGizmoDragging || mPainting || mRoomDragging) {
            mStatus =
                "finish or release the active edit before leaving the tool";
        }
        else if (ConfirmDialog::isOpen()) {
            ConfirmDialog::cancel();
        }
        else if (mDiscardOpen || mSaveAsOpen || mOpenSceneOpen) {
            mDiscardOpen = false;
            mSaveAsOpen = false;
            mOpenSceneOpen = false;
        }
        else if (mPalette.open) {
            mPalette.open = false;
        }
        else if (mMaterialMode) {
            setMode(false);
        }
        else if (!mState.selection.empty()) {
            mState.selection.clear();
        }
        else if (mState.tool != Tool::Select) {
            mState.tool = Tool::Select;
        }
        else {
            requestDiscard(Discard::Quit);
        }
    }
}

// Editor verbs the console can reach. Every one of them is a call the menus
// already make: the console is a second way in, not a second implementation.
void EditorApp::installConsoleCommands()
{
    mConsole.captureEngineLog();
    mConsole.registerCommand("quit", "close the editor",
                             [this](const eng::DebugConsole::Args&) {
                                 requestDiscard(Discard::Quit);
                             });
    mConsole.registerCommand("cook", "cook the open scene to a runtime map",
                             [this](const eng::DebugConsole::Args&) {
                                 std::string mapPath;
                                 if (cookScene(mapPath))
                                     mConsole.print(eng::log::Level::Info,
                                                    "editor",
                                                    "cooked -> " + mapPath);
                             });
    mConsole.registerCommand(
        "play", "cook and launch a playtest",
        [this](const eng::DebugConsole::Args&) { runPlaytest(); });
    mConsole.registerCommand(
        "frame", "frame the selection, or the whole scene",
        [this](const eng::DebugConsole::Args&) { frameSelectionOrAll(); });
    mConsole.registerCommand(
        "scene", "report the open scene",
        [this](const eng::DebugConsole::Args&) {
            mConsole.print(eng::log::Level::Info, "editor",
                           mState.scenePath.empty() ? std::string("<unsaved>")
                                                    : mState.scenePath);
            mConsole.print(eng::log::Level::Info, "editor",
                           std::to_string(mState.document.entities.size()) +
                               " entities, " +
                               (mState.dirty ? "unsaved changes" : "clean"));
        });
    mConsole.registerCommand(
        "save", "write the open scene to disk",
        [this](const eng::DebugConsole::Args&) { saveScene(); });
    mConsole.registerCommand(
        "open", "open a scene: open <path>, or bare to list what is there",
        [this](const eng::DebugConsole::Args& args) {
            const std::string directory = mState.assetRoot + "/scenes";
            if (args.size() < 2) {
                for (const SceneEntry& entry : listScenes(directory))
                    mConsole.print(eng::log::Level::Info, "editor", entry.name);
                return;
            }
            // A bare name is resolved against the scenes directory, so the
            // console echoes what the Open dialog lists.
            std::filesystem::path path(args[1]);
            if (!path.has_parent_path())
                path = std::filesystem::path(directory) / path;
            if (!path.has_extension())
                path.replace_extension(".scn");
            requestOpen(path.string());
        },
        [this](const eng::DebugConsole::Args&) {
            std::vector<std::string> names;
            for (const SceneEntry& entry : listScenes(mState.assetRoot + "/scenes"))
                names.push_back(entry.name);
            return names;
        });
}

void EditorApp::onUpdate(const eng::FrameContext& f)
{
    tickAutosave(f.dt);
    eng::Renderer& renderer = f.engine.renderer();
    // Editing particles.toml in a text editor should land without a restart,
    // the same as editing it through the panel. Re-registration keeps effect
    // ids stable, so live instances survive the reload.
    mParticles.reloadIfChanged(renderer);
    // Repro hook: walk the material list the way a user scrubbing it would.
    if (mCycleMaterials && !mMaterialNames.empty() && mStage.thumbnailBuilt()) {
        mCycleIndex = (mCycleIndex + 1) % mMaterialNames.size();
        const std::string& name = mMaterialNames[mCycleIndex];
        mSelectedMaterial = name;
        mStage.setThumbnailMaterial(renderer, name);
        if (mMaterialMode)
            mStage.setMaterial(renderer, name);
    }

    // The swatch turns whether or not the staging mode is open: it is a live
    // preview in the corner of the material list, not a mode.
    if (mThumbAutoSpin && mStage.thumbnailBuilt())
        mStage.spinThumbnail(renderer, mStage.thumbnailSpin() + 0.6f * f.dt);

    if (mMaterialMode) {
        if (mStageAutoSpin)
            mStage.setSpin(renderer, mStage.spin() + mStageSpinSpeed * f.dt);
    }
    else {
        mPreview->sync(mState.document, mState.catalog);
        // Everything above the storey being edited is cut away, so a ceiling
        // does not become a lid over the top-down view. The work plane picks
        // the storey; raising it one cell reveals the level above.
        mPreview->setCeilingCut(renderer, mState.gridState.level +
                                              mState.grid.cell * 0.5f);
        mPreview->setHiddenEntities(renderer, mState.hidden);
    }
    // Walk mode hands the renderer the player's eye and the game's field of
    // view, so the viewport shows exactly the frame the player will get.
    renderer.setEditorCameraPose(mState.camera.activeEye(),
                                 mState.camera.activeOrientation(),
                                 mState.camera.activeFovDeg());
    if (mMaterialMode)
        renderer.setDebugLines({}); // the checkerboard is the reference here
    else
        updateGridLines(renderer);
    renderer.frameStats(mBatches, mTriangles);

    int exitCode = 0;
    if (mPlaytest.running() && !pollGame(mPlaytest, exitCode)) {
        mStatus = exitCode == 0
                      ? "playtest finished"
                      : "playtest exited with code " +
                            std::to_string(exitCode) + " -- see artifacts/playtest.log";
    }
}

// Editor lighting for level editing: bright and flat, because the job there is
// to see where things are, not how they are lit. The staging scene deliberately
// replaces this with something much darker.
void EditorApp::applySceneEnvironment(eng::Renderer& renderer)
{
    if (mGameLighting) {
        // The scene's own light. Flat work light answers "where is this thing";
        // it cannot answer "does the eye go where I want it to", because that
        // question is entirely about contrast -- and a level whose critical
        // path only reads under the editor's floodlight does not read at all.
        //
        // The palette the level *asks* for, applied through the same loader the
        // game uses. An approximation here would be worse than nothing: the
        // point of the switch is to answer a question about the shipped look,
        // and an editor that answers it with its own numbers is an editor that
        // lies about the level.
        RenderPalette palette;
        const std::string wanted = mState.document.palette.empty()
                                       ? std::string("dungeon")
                                       : mState.document.palette;
        if (loadRenderPalette(mPalettesPath, wanted, palette)) {
            applyRenderPalette(renderer, palette, {}, {});
            renderer.setEditorViewportBackground(palette.backgroundSrgb);
            return;
        }
        renderer.setBackground({0.02f, 0.02f, 0.03f});
        renderer.setAmbient({0.10f, 0.10f, 0.13f});
        renderer.setFog({0.02f, 0.02f, 0.03f}, 0.035f);
        renderer.setEditorViewportBackground({0.02f, 0.02f, 0.03f});
        return;
    }
    renderer.setBackground({0.05f, 0.055f, 0.07f});
    renderer.setAmbient({0.45f, 0.46f, 0.5f});
    renderer.setFog({0.05f, 0.055f, 0.07f}, 0.0f);
    renderer.setEditorViewportBackground({0.10f, 0.11f, 0.13f});
}

void EditorApp::updateGridLines(eng::Renderer& renderer)
{
    static std::vector<eng::Renderer::DebugLine> lines;
    lines.clear();

    const float level = mState.gridState.level;
    // Centred on the camera so the grid never runs out from under the view, and
    // coarsened with height so it still reads as ground from a framing shot.
    const glm::vec3 eye = mState.camera.activeEye();
    const GridView view =
        gridViewFor(mState.grid.cell, eye.y - level, kGridRadius);
    mGridMultiple = view.multiple;
    const float cell = view.cell;
    const int centreCol = int(std::floor(eye.x / cell));
    const int centreRow = int(std::floor(eye.z / cell));

    const glm::vec3 minor{0.22f, 0.24f, 0.30f};
    const glm::vec3 major{0.38f, 0.42f, 0.52f};
    const glm::vec3 axisX{0.55f, 0.25f, 0.28f};
    const glm::vec3 axisZ{0.25f, 0.40f, 0.60f};

    for (int i = -view.radius; i <= view.radius; ++i) {
        const int col = centreCol + i;
        const int row = centreRow + i;
        const float x = float(col) * cell;
        const float z = float(row) * cell;
        const float far = float(view.radius) * cell;
        // Every fourth line brighter, so distances are readable at a glance.
        const glm::vec3 colourX =
            col == 0 ? axisZ : (col % 4 == 0 ? major : minor);
        const glm::vec3 colourZ =
            row == 0 ? axisX : (row % 4 == 0 ? major : minor);
        lines.push_back({{x, level, float(centreRow) * cell - far},
                         {x, level, float(centreRow) * cell + far},
                         colourX});
        lines.push_back({{float(centreCol) * cell - far, level, z},
                         {float(centreCol) * cell + far, level, z},
                         colourZ});
    }
    renderer.setDebugLines(lines);
}

std::vector<PaletteAction> EditorApp::paletteActions()
{
    std::vector<PaletteAction> actions;
    const auto add = [&](std::string label, std::string group,
                         std::string shortcut, bool enabled,
                         std::function<void()> run, std::string detail = {}) {
        PaletteAction action;
        action.label = std::move(label);
        action.group = std::move(group);
        action.shortcut = std::move(shortcut);
        action.detail = std::move(detail);
        action.enabled = enabled;
        action.run = std::move(run);
        actions.push_back(std::move(action));
    };

    // --- scene ---------------------------------------------------------------
    for (const SceneTemplate which : {SceneTemplate::Empty, SceneTemplate::Room,
                                      SceneTemplate::TechDemo}) {
        add(std::string("New scene: ") + sceneTemplateName(which), "scene", "",
            true, [this, which] { requestDiscard(Discard::NewScene, which); });
    }
    add("Open scene...", "scene", "Ctrl+O", true,
        [this] { mOpenSceneOpen = true; });
    for (const std::string& path : mRecent.paths()) {
        add("Open " + std::filesystem::path(path).filename().string(), "scene",
            "", true, [this, path] { requestOpen(path); }, "recent");
    }
    add("Save scene", "scene", "Ctrl+S", !mState.scenePath.empty(),
        [this] { saveScene(); }, mState.scenePath);
    add("Save scene as...", "scene", "", true, [this] {
        mSaveAsOpen = true;
        std::snprintf(mSaveAsPath, sizeof(mSaveAsPath), "%s",
                      mState.scenePath.empty()
                          ? (mState.assetRoot + "/scenes/untitled.scn").c_str()
                          : mState.scenePath.c_str());
    });
    add("Reload scene from disk", "scene", "Ctrl+R", !mState.scenePath.empty(),
        [this] { requestDiscard(Discard::Reload); });
    add("Recover autosave", "scene", "",
        autosaveIsStale(mState.scenePath, mState.assetRoot + "/scenes"),
        [this] { recoverAutosave(); },
        "the backup written while the last session was unsaved");
    add("Cook scene to a runtime map", "scene", "F6", true, [this] {
        std::string mapPath;
        cookScene(mapPath);
    });
    add(mPlaytest.running() ? "Stop playtest" : "Run playtest", "scene", "F5",
        true, [this] { runPlaytest(); },
        mPlayFromCamera ? "from the camera" : "from the player spawn");
    add(mPlayFromCamera ? "Playtest from the player spawn instead"
                        : "Playtest from the camera instead",
        "scene", "", true, [this] { mPlayFromCamera = !mPlayFromCamera; });
    add("Quit editor", "scene", "Esc", true,
        [this] { requestDiscard(Discard::Quit); });

    // --- edit ----------------------------------------------------------------
    // Carrying the edit's own name is the point: "Undo place kit.wall_arch"
    // answers the question the author actually has before they commit to it.
    add("Undo " + mCommands.undoLabel(), "edit", "Ctrl+Z", mCommands.canUndo(),
        [this] { applyHistory(false); });
    add("Redo " + mCommands.redoLabel(), "edit", "Ctrl+Shift+Z",
        mCommands.canRedo(), [this] { applyHistory(true); });
    const bool hasSelection = !mState.selection.empty();
    add("Copy selection", "edit", "Ctrl+C", hasSelection,
        [this] { copySelection(false); });
    add("Cut selection", "edit", "Ctrl+X", hasSelection,
        [this] { copySelection(true); });
    add("Paste", "edit", "Ctrl+V", !mClipboard.empty(),
        [this] { pasteClipboard(); },
        mClipboard.empty() ? "" : std::to_string(mClipboard.size()) + " copied");
    add("Duplicate selection", "edit", "Ctrl+D", hasSelection,
        [this] { duplicateSelection(); });
    add("Delete selection", "edit", "Del", hasSelection,
        [this] { deleteSelection(); });
    add("Select nothing", "edit", "Esc", hasSelection,
        [this] { mState.selection.clear(); });
    add("Parent selection to first selected", "edit", "",
        mState.selection.size() > 1, [this] { parentSelectionToPrimary(); },
        "makes a composed object that moves as one");
    add("Detach selection from parent", "edit", "", hasSelection,
        [this] { detachSelection(); });

    // --- tools and view ------------------------------------------------------
    add("Select tool", "tool", "Q", true, [this] { mState.tool = Tool::Select; });
    add("Place tool", "tool", "W", true, [this] { mState.tool = Tool::Place; });
    add("Room tool", "tool", "E", true, [this] { mState.tool = Tool::Room; });
    add("Focus selection", "view", "F", true, [this] { frameSelectionOrAll(); });
    add("Toggle snap to grid", "view", "G", true,
        [this] { mState.gridState.snap = !mState.gridState.snap; });
    add("Raise work plane", "view", "PageUp", true,
        [this] { mState.gridState.level += mState.grid.cell; });
    add("Lower work plane", "view", "PageDown", true,
        [this] { mState.gridState.level -= mState.grid.cell; });
    add("Reset work plane to zero", "view", "Home", true,
        [this] { mState.gridState.level = 0.0f; });
    add("Cycle gizmo: translate / rotate / scale", "view", "R", true,
        [this] { mGizmoOperation = (mGizmoOperation + 1) % 3; });
    add(mGizmoLocal ? "Gizmo axes: world" : "Gizmo axes: local", "view", "X",
        true, [this] { mGizmoLocal = !mGizmoLocal; });
    add(mState.camera.walking() ? "Leave walk camera" : "Walk the level", "view",
        "V", true, [this] { toggleWalk(); });
    add(mGameLighting ? "Editor lighting" : "Game lighting", "view", "", true,
        [this] {
            mGameLighting = !mGameLighting;
            if (mEngine)
                applySceneEnvironment(mEngine->renderer());
        });
    add(mMaterialMode ? "Leave material stage" : "Material stage", "view", "",
        true, [this] { setMode(!mMaterialMode); });
    add("Console", "view", "`", true, [this] { mConsole.toggle(); });
    add("Shortcuts", "view", "F1", true, [this] { mHelpOpen = !mHelpOpen; });
    add("Settings", "view", "", true, [this] { mSettingsOpen = !mSettingsOpen; });
    // Findable by what it is for, not only by what it is called: "autosave" is
    // the word somebody types when they want this window.
    add("Autosave settings", "view", "", true,
        [this] { mSettingsOpen = true; });
    add(mShowEntityGizmos ? "Hide entity marks" : "Show entity marks", "view",
        "", true, [this] { mShowEntityGizmos = !mShowEntityGizmos; });
    add(mShowFrameStats ? "Hide frame stats" : "Show frame stats", "view", "",
        true, [this] { mShowFrameStats = !mShowFrameStats; });
    for (const eng::RenderPresetInfo& preset : eng::renderPresets()) {
        add(std::string("Render preset: ") + preset.name, "view", "",
            mEngine != nullptr,
            [this, id = preset.id] {
                if (mEngine)
                    mEngine->setRenderPreset(id);
            },
            mEngine && mEngine->renderPreset() == preset.id ? "current" : "");
    }

    // --- authoring -----------------------------------------------------------
    const auto gameplay = [&](const char* label, Gameplay kind) {
        add(std::string("Add ") + label, "add", "", true,
            [this, kind] { addGameplayEntity(kind); });
    };
    gameplay("group (empty node to parent things to)", Gameplay::Group);
    gameplay("player spawn", Gameplay::PlayerSpawn);
    gameplay("exit", Gameplay::Exit);
    gameplay("marker", Gameplay::Marker);
    gameplay("enemy spawn", Gameplay::EnemySpawn);
    gameplay("pickup", Gameplay::Pickup);
    gameplay("trigger volume", Gameplay::Trigger);
    gameplay("point light", Gameplay::PointLight);
    gameplay("directional light", Gameplay::DirectionalLight);

    // Every kit piece, by name. This is the half of the palette that pays for
    // it: the catalogue is hundreds of pieces behind a role header, and an
    // author who knows the piece is called `wall_arch` should not have to find
    // which role it was filed under.
    for (const std::string& role : mState.catalog.roles()) {
        for (const KitPiece* piece : mState.catalog.byRole(role)) {
            add(std::string("Place ") + piece->id, "place", "", true,
                [this, id = piece->id] {
                    mState.brushPrefab = id;
                    mState.tool = Tool::Place;
                },
                role);
        }
    }
    return actions;
}

void EditorApp::onGui(const eng::FrameContext& f)
{
    // Ctrl+P is read here rather than through the keybind table because the
    // palette's own text field owns the keyboard while it is open, and the
    // block in onFrameBegin is deliberately mute in exactly that case.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P))
        openPalette(mPalette);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##editor_root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleVar(3);

    drawMenuBar(f);
    const ImGuiID dock = ImGui::GetID("##editor_dock");
    // No passthru: the central node is the viewport image, not a window onto
    // whatever the main camera happens to be pointing at.
    ImGui::DockSpace(dock, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    if (!mLayoutBuilt) {
        mLayoutBuilt = true;
        // Only build the default layout when imgui has no saved one, so a
        // rearranged workspace survives a restart.
        if (ImGui::DockBuilderGetNode(dock) == nullptr ||
            ImGui::DockBuilderGetNode(dock)->IsEmpty()) {
            ImGui::DockBuilderRemoveNode(dock);
            ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dock, viewport->WorkSize);

            ImGuiID centre = dock;
            const ImGuiID top = ImGui::DockBuilderSplitNode(
                centre, ImGuiDir_Up, 0.06f, nullptr, &centre);
            const ImGuiID bottom = ImGui::DockBuilderSplitNode(
                centre, ImGuiDir_Down, 0.10f, nullptr, &centre);
            const ImGuiID right = ImGui::DockBuilderSplitNode(
                centre, ImGuiDir_Right, 0.22f, nullptr, &centre);
            const ImGuiID left = ImGui::DockBuilderSplitNode(
                centre, ImGuiDir_Left, 0.20f, nullptr, &centre);

            ImGui::DockBuilderDockWindow("Toolbar", top);
            ImGui::DockBuilderDockWindow("Status", bottom);
            // Inspector above, Material below it -- the arrangement every DCC
            // and engine editor converged on, because the preview is read while
            // editing the thing above it.
            ImGuiID rightTop = right;
            const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(
                rightTop, ImGuiDir_Down, 0.45f, nullptr, &rightTop);
            ImGui::DockBuilderDockWindow("Inspector", rightTop);
            // Material and Particles share that lower slot as tabs: both are
            // the same job -- pick an asset from a list and tune it against the
            // viewport -- and only one of them is ever the thing being worked
            // on. A window left out of the builder floats loose over the
            // dockspace, which is what this one did.
            ImGui::DockBuilderDockWindow("Material", rightBottom);
            ImGui::DockBuilderDockWindow("Particles", rightBottom);
            ImGui::DockBuilderDockWindow("Outliner", left);
            ImGui::DockBuilderDockWindow("Catalog", left);
            ImGui::DockBuilderDockWindow("Issues", bottom);
            ImGui::DockBuilderDockWindow("Console", bottom);
            // The 2D viewport shares the centre slot as a tab beside the 3D
            // one: they are two views of one game, and docking the HUD off to
            // the side would make it a widget rather than a viewport.
            //
            // UI first, Viewport second: the last window docked into a node is
            // the one selected, and an editor whose subject is the 3D scene
            // must not open on the HUD preview.
            ImGui::DockBuilderDockWindow("UI", centre);
            ImGui::DockBuilderDockWindow("Viewport", centre);
            ImGui::DockBuilderFinish(dock);
            // The builder selects the last window docked into a node, so
            // Particles wins the tab it shares with Material. A focus asked for
            // before this point is overwritten by it; asked for here, it lands.
            if (mMaterialMode) mFocusMaterialFrames = 4;
        }
    }
    ImGui::End();

    if (mFocusPanelFrames > 0)
        --mFocusPanelFrames;

    drawToolbar();
    drawViewport(f);
    drawUiStage();
    drawOutliner();
    drawCatalog();
    drawInspector();
    drawIssues();
    mConsole.draw();
    drawMaterialPanel();
    drawParticlePanel();
    drawStatusBar();
    drawHelp();
    drawSettings();
    captureSettings();
    drawSaveAsPopup();
    drawOpenScenePopup();
    drawDiscardPopup();
    ConfirmDialog::draw();
    runSelfTest(f.engine);
    // Last, and over everything: the palette is the only surface that is not
    // part of the workspace.
    if (mPalette.open)
        drawCommandPalette(mPalette, paletteActions());
}

void EditorApp::drawMenuBar(const eng::FrameContext& f)
{
    if (!ImGui::BeginMenuBar())
        return;
    if (ImGui::BeginMenu("Scene")) {
        if (ImGui::BeginMenu("New")) {
            // The same templates scene_cook --template generates, so what the
            // menu builds and what ships in assets/scenes cannot diverge.
            for (const SceneTemplate which :
                 {SceneTemplate::Empty, SceneTemplate::Room,
                  SceneTemplate::TechDemo}) {
                if (ImGui::MenuItem(sceneTemplateName(which)))
                    requestDiscard(Discard::NewScene, which);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            mOpenSceneOpen = true;
        if (ImGui::BeginMenu("Open recent", !mRecent.paths().empty())) {
            for (const std::string& path : mRecent.paths()) {
                const std::string name =
                    std::filesystem::path(path).filename().string();
                if (ImGui::MenuItem(name.c_str()))
                    requestOpen(path);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", path.c_str());
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !mState.scenePath.empty()))
            saveScene();
        if (ImGui::MenuItem("Save as...")) {
            mSaveAsOpen = true;
            std::snprintf(
                mSaveAsPath, sizeof(mSaveAsPath), "%s",
                mState.scenePath.empty()
                    ? (mState.assetRoot + "/scenes/untitled.scn").c_str()
                    : mState.scenePath.c_str());
        }
        if (ImGui::MenuItem("Reload", "Ctrl+R", false,
                            !mState.scenePath.empty()))
            requestDiscard(Discard::Reload);
        if (ImGui::MenuItem(
                "Recover autosave", nullptr, false,
                autosaveIsStale(mState.scenePath, mState.assetRoot + "/scenes")))
            recoverAutosave();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Esc"))
            requestDiscard(Discard::Quit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const std::string undo = "Undo " + mCommands.undoLabel();
        const std::string redo = "Redo " + mCommands.redoLabel();
        if (ImGui::MenuItem(undo.c_str(), "Ctrl+Z", false, mCommands.canUndo()))
            applyHistory(false);
        // Commands::label has documented itself as "shown in the Edit menu and
        // the undo tooltip" since it was written; this is the tooltip.
        eng::imguihint::hover("editor.undo");
        if (ImGui::MenuItem(redo.c_str(), "Ctrl+Shift+Z", false,
                            mCommands.canRedo()))
            applyHistory(true);
        eng::imguihint::hover("editor.redo");
        ImGui::Separator();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, !mState.selection.empty()))
            copySelection(false);
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, !mState.selection.empty()))
            copySelection(true);
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !mClipboard.empty()))
            pasteClipboard();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false,
                            !mState.selection.empty()))
            duplicateSelection();
        if (ImGui::MenuItem("Delete", "Del", false, !mState.selection.empty()))
            deleteSelection();
        ImGui::Separator();
        // Preferences, under Edit where every other editor keeps them. The
        // autosave interval used to be a constant in this file.
        ImGui::MenuItem("Settings...", nullptr, &mSettingsOpen);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Play")) {
        if (ImGui::MenuItem(mPlaytest.running() ? "Stop playtest" : "Run",
                            "F5"))
            runPlaytest();
        if (ImGui::MenuItem("Cook only", "F6")) {
            std::string mapPath;
            cookScene(mapPath);
        }
        ImGui::Separator();
        ImGui::MenuItem("Start where the camera is", nullptr, &mPlayFromCamera);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off: start at the scene's player spawn, which is "
                              "how the arrival itself gets checked.");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Command palette", "Ctrl+P"))
            openPalette(mPalette);
        ImGui::MenuItem("Shortcuts", "F1", &mHelpOpen);
        ImGui::Separator();
        // The look the game will ship with, switchable while authoring. A room
        // that reads under flat editor light and disappears under the dungeon
        // profile is a room that has not been checked -- and the check used to
        // require cooking the map and launching the game.
        if (ImGui::BeginMenu("Render preset")) {
            const int current = mEngine ? mEngine->renderPreset() : 0;
            for (const eng::RenderPresetInfo& preset : eng::renderPresets()) {
                if (ImGui::MenuItem(preset.name, nullptr,
                                    preset.id == current) &&
                    mEngine)
                    mEngine->setRenderPreset(preset.id);
            }
            ImGui::EndMenu();
        }
        ImGui::MenuItem("Entity marks", nullptr, &mShowEntityGizmos);
        ImGui::MenuItem("Trigger and light volumes", nullptr,
                        &mShowGizmoVolumes);
        ImGui::MenuItem("Frame stats", nullptr, &mShowFrameStats);
        ImGui::Separator();
        ImGui::MenuItem("Snap to grid", "G", &mState.gridState.snap);
        bool console = mConsole.visible();
        if (ImGui::MenuItem("Console", "`", &console))
            mConsole.setVisible(console);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void EditorApp::drawToolbar()
{
    if (ImGui::Begin("Toolbar", nullptr, kPanelFlags)) {
        if (ImGui::RadioButton("Select (Q)", mState.tool == Tool::Select))
            mState.tool = Tool::Select;
        ImGui::SameLine();
        if (ImGui::RadioButton("Place (W)", mState.tool == Tool::Place))
            mState.tool = Tool::Place;
        ImGui::SameLine();
        if (ImGui::RadioButton("Room (E)", mState.tool == Tool::Room))
            mState.tool = Tool::Room;

        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        ImGui::Text("grid %.2g m", double(mState.gridState.step()));
        if (mGridMultiple > 1) {
            // The drawn grid coarsened with the camera's height. Say so: a line
            // spacing that silently changes meaning is worse than no grid.
            ImGui::SameLine();
            ImGui::TextDisabled("(drawn x%d)", mGridMultiple);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("["))
            mState.gridState.coarser();
        ImGui::SameLine();
        if (ImGui::SmallButton("]"))
            mState.gridState.finer();
        ImGui::SameLine();
        ImGui::Checkbox("snap", &mState.gridState.snap);
        ImGui::SameLine();
        ImGui::Text("| level %.1f m", double(mState.gridState.level));
        ImGui::SameLine();
        if (ImGui::SmallButton("-"))
            mState.gridState.level -= mState.grid.cell;
        ImGui::SameLine();
        if (ImGui::SmallButton("+"))
            mState.gridState.level += mState.grid.cell;

        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        if (mMaterialMode) {
            ImGui::TextDisabled("stage Y rotate");
        }
        else {
            // Gizmo state stays visible and explicit. Controls lock during a
            // drag so one pointer transaction cannot change interpretation
            // halfway through.
            static const char* kOps[3] = {"Move", "Rotate", "Scale"};
            ImGui::BeginDisabled(mGizmoDragging);
            ImGui::SetNextItemWidth(90.0f);
            ImGui::Combo("##gizmoop", &mGizmoOperation, kOps, 3);
            ImGui::SameLine();
            const bool worldForced = mState.selection.size() > 1;
            const bool localForced = mGizmoOperation == 2 && !worldForced;
            const char* spaceLabel = worldForced   ? "world (multi)"
                                     : localForced ? "local (scale)"
                                     : mGizmoLocal ? "local (X)"
                                                   : "world (X)";
            ImGui::BeginDisabled(worldForced || localForced);
            if (ImGui::SmallButton(spaceLabel))
                mGizmoLocal = !mGizmoLocal;
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(worldForced
                                      ? "Multiple objects use world axes."
                                      : "Axes the gizmo acts along.");
        }

        // Framing, walking, lighting, playing and cooking used to live here as
        // well. They are questions about what is on screen, so they moved into
        // the strip above the viewport, next to the thing they change. Two
        // controls for one piece of state is worse than either alone: the one
        // you are not looking at is the one that looks stale.
        if (mState.dirty) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.38f, 1.0f), "* unsaved");
        }
    }
    ImGui::End();
}

// A compact strip of toggles across the top of the 3D view.
//
// Everything here changes what the viewport *shows* without changing the
// document. That is the line: the tools, the grid step and the work plane are
// edits waiting to happen and live in the Toolbar panel; these are ways of
// looking, and an author flips them constantly while placing.
void EditorApp::drawViewportToolbar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

    // Play and Cook keep their words: they are verbs with consequences (one
    // launches a process, the other writes a file), and a shape is the wrong
    // affordance for a thing you should be sure about before clicking.
    if (ImGui::Button(mPlaytest.running() ? "Stop" : "Play"))
        runPlaytest();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("F5 -- save, cook and launch the game on this scene");
    ImGui::SameLine();
    if (ImGui::Button("Cook")) {
        std::string mapPath;
        cookScene(mapPath);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("F6 -- cook to a runtime map without playing");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // The view switches are icons: they are states, they are flipped
    // constantly, and four labelled checkboxes were most of the strip.
    if (iconButton(Icon::Marker, "##marks",
                   "entity marks -- spawns, lights, triggers, markers",
                   mShowEntityGizmos))
        mShowEntityGizmos = !mShowEntityGizmos;
    ImGui::SameLine();
    if (iconButton(Icon::Collider, "##volumes",
                   "trigger boxes, collider boxes and light ranges",
                   mShowGizmoVolumes))
        mShowGizmoVolumes = !mShowGizmoVolumes;
    ImGui::SameLine();
    if (iconButton(Icon::Trigger, "##stats",
                   "frame cost, in the corner of the view", mShowFrameStats))
        mShowFrameStats = !mShowFrameStats;
    ImGui::SameLine();
    // Lighting is the one switch here that costs something to apply, so it is
    // pushed to the renderer on the frame it changes rather than every frame.
    if (iconButton(Icon::Light, "##gamelight",
                   mGameLighting
                       ? "the scene's own lighting"
                       : "the editor's flat work light -- click for the "
                         "scene's own",
                   mGameLighting)) {
        mGameLighting = !mGameLighting;
        if (mEngine)
            applySceneEnvironment(mEngine->renderer());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    if (ImGui::Button("Frame"))
        frameSelectionOrAll();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("F -- fit the selection, or the whole scene");
    ImGui::SameLine();
    if (iconButton(Icon::Spawn, "##walk",
                   mState.camera.walking()
                       ? "standing at the player's eye -- click to leave"
                       : "V -- judge the room from where the player's head "
                         "will be",
                   mState.camera.walking()))
        toggleWalk();
    ImGui::PopStyleVar();
    ImGui::Separator();
}

void EditorApp::drawViewportStats(const eng::FrameContext& f)
{
    if (!mShowFrameStats || mViewportW < 8.0f)
        return;

    FrameStats sample;
    sample.frameMs = f.dt * 1000.0f;
    sample.fps = f.dt > 0.0f ? 1.0f / f.dt : 0.0f;
    sample.batches = mBatches;
    sample.triangles = mTriangles;
    sample.entities = mState.document.entities.size();
    sample.selected = mState.selection.size();
    // Twenty frames or so to settle: slow enough to read, fast enough that a
    // stall caused by the thing just placed is still attributable to it.
    mFrameStats.update(sample, 0.1f);

    drawFrameStats(ImGui::GetWindowDrawList(), mFrameStats.smoothed(),
                   mFrameBudget, mViewportX, mViewportY, mViewportW, mViewportH);
}

void EditorApp::drawViewport(const eng::FrameContext& f)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("Viewport", nullptr,
                     kPanelFlags | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse)) {
        // Above the image, inside the panel: these toggles answer questions
        // about what is on screen, so they belong next to the screen rather
        // than in a toolbar at the far top of the window.
        drawViewportToolbar();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        mViewportX = pos.x;
        mViewportY = pos.y;

        if (size.x > 8.0f && size.y > 8.0f) {
            if (int(size.x) != int(mViewportW) ||
                int(size.y) != int(mViewportH)) {
                mViewportW = size.x;
                mViewportH = size.y;
                f.engine.renderer().resizeEditorViewport(int(size.x),
                                                         int(size.y));
            }
            const uint64_t texture =
                f.engine.renderer().editorViewportTextureId();
            if (texture != 0) {
                // Default uv: OGRE's render-to-texture already hands back a
                // top-down image, so flipping V here turned the whole world
                // upside down.
                ImGui::Image(static_cast<ImTextureID>(texture), size);
            }
            else {
                ImGui::TextUnformatted("offscreen viewport unavailable");
            }
        }
        mViewportHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (mMaterialMode || mState.tool != Tool::Place || !mViewportHovered ||
            mFlying)
            mPreview->hidePlacementGhost();
        // Gizmo first, then picking: a click that lands on a gizmo handle is a
        // drag, not a selection change.
        if (mMaterialMode) {
            // Only the test shape may be manipulated: a reference stage whose
            // floor and lights can be dragged out of alignment has stopped
            // being a reference.
            drawStageGizmo(f);
        }
        else if (mState.tool == Tool::Room) {
            drawRoomPreview(f);
            if (mViewportHovered && !mFlying) {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (hoveredCell(mRoomStartCol, mRoomStartRow))
                        mRoomDragging = true;
                }
            }
            if (mRoomDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                mRoomDragging = false;
                commitRoom();
            }
        }
        else if (mState.tool == Tool::Place) {
            drawPlacementGhost();
            if (mViewportHovered && !mFlying) {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    mPainting = true;
                    mPaintedSlots.clear();
                    mPaintParts.clear();
                }
                if (mPainting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    CellPlacement cell;
                    XformAuthor transform;
                    if (hoveredPlacement(cell, transform))
                        placeAt(cell, transform);
                }
            }
            if (mPainting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                mPainting = false;
                if (!mPaintParts.empty()) {
                    // The stroke was applied straight to the document so the
                    // ghost had something to follow. Roll that back and let the
                    // command apply it properly: the end state is identical,
                    // and now the entry has a real apply for redo instead of a
                    // no-op that would lose the pieces.
                    const std::string label =
                        "place " + std::to_string(mPaintParts.size()) + " x " +
                        mState.brushPrefab;
                    for (const AuthorId& id : mPaintedIds)
                        mState.document.remove(id);
                    runCommand(makeComposite(label, std::move(mPaintParts)));
                    mPaintParts.clear();
                    mPaintedIds.clear();
                    mPreview->invalidate();
                }
            }
        }
        else {
            drawGizmo(f);
            handleViewportPicking(f);
        }
        // Over the image and under ImGuizmo: the marks are affordances for the
        // half of a level that has no mesh, and the manipulator must stay on
        // top of everything.
        drawEntityGizmos();
        drawViewportStats(f);
    }
    else {
        mViewportHovered = false;
        mPreview->hidePlacementGhost();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// Camera basis shared by the gizmo and the picker, so a click and the handle it
// lands on can never disagree about where the camera is.
static void cameraMatrices(const EditorCamera& camera, float aspect,
                           glm::mat4& view, glm::mat4& projection)
{
    const glm::vec3 eye = camera.activeEye();
    const glm::quat orientation = camera.activeOrientation();
    const glm::vec3 forward = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 up = orientation * glm::vec3(0.0f, 1.0f, 0.0f);
    view = glm::lookAt(eye, eye + forward, up);
    projection = glm::perspective(glm::radians(viewportFovDeg(camera)), aspect,
                                  0.05f, 4000.0f);
}

const std::vector<GizmoMark>& EditorApp::entityGizmoMarks()
{
    // Walks every entity, and the viewport is drawn while the gizmo is being
    // dragged -- so it follows the document's revision, like the outliner.
    if (mGizmoMarksRevision != mState.document.revision) {
        mGizmoMarksRevision = mState.document.revision;
        mGizmoMarks = collectGizmoMarks(mState.document);
    }
    return mGizmoMarks;
}

void EditorApp::drawEntityGizmos()
{
    if (!mShowEntityGizmos || mMaterialMode || mViewportW < 8.0f)
        return;
    glm::mat4 view, projection;
    cameraMatrices(mState.camera, mViewportW / mViewportH, view, projection);
    const glm::mat4 viewProjection = projection * view;

    GizmoOverlay overlay;
    overlay.viewProjection = &viewProjection;
    overlay.viewportOrigin = glm::vec2(mViewportX, mViewportY);
    overlay.viewportSize = glm::vec2(mViewportW, mViewportH);
    overlay.selected = &mState.selection;
    overlay.volumes = mShowGizmoVolumes;
    // A camera frustum is judged against the rectangle it is drawn in, so it is
    // drawn at the viewport's aspect rather than a nominal one.
    if (mViewportH > 0.0f)
        overlay.aspect = float(mViewportW) / float(mViewportH);

    // What the cursor is over, resolved with the same call the click uses, so
    // the mark that lights up is always the mark that would be selected.
    AuthorId hovered;
    if (mViewportHovered && !mFlying) {
        const ImVec2 mouse = ImGui::GetMousePos();
        if (const GizmoMark* mark = pickGizmoMark(
                entityGizmoMarks(), viewProjection,
                glm::vec2(mViewportX, mViewportY),
                glm::vec2(mViewportW, mViewportH), glm::vec2(mouse.x, mouse.y),
                12.0f)) {
            hovered = mark->id;
            overlay.hovered = &hovered;
        }
    }
    drawGizmoMarks(ImGui::GetWindowDrawList(), entityGizmoMarks(), overlay);
}

void EditorApp::handleViewportPicking(const eng::FrameContext& f)
{
    // The height guard is not paranoia: the ray build divides by it, and on the
    // first frame -- or with the panel dragged shut -- it is still zero, which
    // would put a NaN into the picker and select whatever the comparison
    // happened to answer.
    if (!mViewportHovered || mFlying || ImGuizmo::IsUsingAny() || mGizmoHovered)
        return;
    if (mViewportW < 8.0f || mViewportH < 8.0f)
        return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return;

    // A mark under the cursor wins outright. Its entity's own bounds are a one
    // metre box somewhere inside a room, so a ray test against it is luck --
    // but the icon is drawn exactly where the author is already looking.
    const ImVec2 mouse = ImGui::GetMousePos();
    glm::mat4 view, projection;
    cameraMatrices(mState.camera, mViewportW / mViewportH, view, projection);
    if (const GizmoMark* mark = pickGizmoMark(
            entityGizmoMarks(), projection * view,
            glm::vec2(mViewportX, mViewportY), glm::vec2(mViewportW, mViewportH),
            glm::vec2(mouse.x, mouse.y), 12.0f)) {
        selectAndReveal(mark->id, ImGui::GetIO().KeyShift);
        return;
    }

    const Ray ray = mouseRay();

    // Tested against catalogue bounds rather than the loaded meshes: an entity
    // has to be selectable even before its mesh is on disk, which is exactly
    // when the author most needs to click it and see why.
    const AuthorId* hit = nullptr;
    float bestT = 1e30f;
    float bestVolume = 1e30f;
    for (const Entity& entity : mState.document.entities) {
        if (!mPreview->entityVisible(entity.id))
            continue;
        // Hidden is not there; locked is there and not clickable. Locking the
        // floor is how a room gets dressed without picking it forty times.
        if (mState.isHidden(entity.id) || mState.isLocked(entity.id))
            continue;
        glm::vec3 localMin(-0.5f), localMax(0.5f);
        if (const KitPiece* piece = mState.catalog.find(entity.prefab))
            piece->localBoundsMeters(mState.catalog.scale(), localMin,
                                     localMax);
        glm::vec3 min, max;
        transformedBounds(mState.document.worldTransform(entity.id), localMin,
                          localMax, min, max);
        float t = 0.0f;
        if (!rayAabb(ray, min, max, t))
            continue;
        const glm::vec3 size = max - min;
        const float volume = size.x * size.y * size.z;
        // Ties go to the smaller box, so clicking a barrel inside a room does
        // not select the room.
        if (t < bestT - 1e-3f ||
            (std::fabs(t - bestT) <= 1e-3f && volume < bestVolume)) {
            bestT = t;
            bestVolume = volume;
            hit = &entity.id;
        }
    }

    if (!hit) {
        if (!ImGui::GetIO().KeyShift)
            mState.selection.clear();
        return;
    }
    selectAndReveal(resolvePick(*hit), ImGui::GetIO().KeyShift);
}

// A viewport hit is a mesh; this is the entity that click means. The rule lives
// in PickTarget.h, where it can be tested without an editor.
game::content::AuthorId EditorApp::resolvePick(const AuthorId& hit) const
{
    return resolvePickTarget(mState.document, hit, mState.selection,
                             ImGui::GetIO().KeyAlt);
}

void EditorApp::drawGizmo(const eng::FrameContext& f)
{
    mGizmoHovered = false;
    if (mState.selection.empty() || mViewportW < 8.0f)
        return;
    const Entity* primary = mState.document.find(*mState.primary());
    if (!primary)
        return;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    // The panel's rect, not the window's: docked anywhere else and the handles
    // would sit offset from the geometry they manipulate.
    ImGuizmo::SetRect(mViewportX, mViewportY, mViewportW, mViewportH);

    glm::mat4 view, projection;
    cameraMatrices(mState.camera, mViewportW / mViewportH, view, projection);

    // The gizmo sits at the VISUAL CENTRE of the selection, not at the entity's
    // transform origin. Kit pieces are authored with their origin on the floor,
    // so anchoring to it puts the handles at the foot of a wall -- or, with
    // several things selected, at whichever one happens to be primary. The
    // centre of the combined bounds is where every DCC puts it, and it is the
    // point a person means when they say "this selection".
    glm::vec3 boundsMin, boundsMax;
    const bool haveBounds = boundsOf(mState.selection, boundsMin, boundsMax);
    // The primary's *world* transform: for a parented entity the authored one
    // is an offset inside its parent, and anchoring the handles there would put
    // them somewhere the entity is not.
    const WorldTransform primaryWorld =
        mState.document.worldTransform(primary->id);
    const glm::vec3 liveAnchor =
        haveBounds ? (boundsMin + boundsMax) * 0.5f : primaryWorld.position;
    // Once a drag is under way the pivot is whatever it was when the drag
    // started; recomputing it from bounds that the drag itself is changing
    // would make the handles wander out from under the cursor.
    const glm::vec3 anchor = mGizmoDragging ? mDragAnchor : liveAnchor;

    // With one thing selected the gizmo carries that thing's own rotation and
    // scale, which is what makes the handles line up with it. With several, it
    // starts from identity instead: the numbers ImGuizmo then reports ARE the
    // delta to apply to every member, so a rotation turns the whole selection
    // about the anchor rather than spinning one entity in place while the rest
    // sit still -- which is what used to happen.
    const bool multi = mState.selection.size() > 1;
    const glm::vec3 identityScale(1.0f);
    glm::mat4 matrix(1.0f);
    if (mGizmoDragging) {
        matrix = mDragGizmoMatrix;
    }
    else {
        const glm::quat orientation = multi ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
                                            : primaryWorld.orientation;
        matrix = glm::translate(glm::mat4(1.0f), anchor) *
                 glm::mat4_cast(orientation) *
                 glm::scale(glm::mat4(1.0f),
                            multi ? identityScale : primaryWorld.scale);
    }

    const bool rotating = mGizmoOperation == 1;
    const bool scaling = mGizmoOperation == 2;
    const ImGuizmo::OPERATION operation =
        rotating  ? ImGuizmo::ROTATE
        : scaling ? (multi ? ImGuizmo::SCALEU : ImGuizmo::SCALE)
                  : ImGuizmo::TRANSLATE;
    const float step = mState.gridState.step();
    const glm::vec3 translateSnap{step, step, step};
    const float angleStep = (!multi && primary->cell) ? 90.0f : 15.0f;
    const glm::vec3 rotateSnap{angleStep, angleStep, angleStep};
    const glm::vec3 scaleSnap{0.1f, 0.1f, 0.1f};
    const float* snapPtr = nullptr;
    if (mState.gridState.snap) {
        snapPtr = rotating  ? glm::value_ptr(rotateSnap)
                  : scaling ? glm::value_ptr(scaleSnap)
                            : glm::value_ptr(translateSnap);
    }

    // Local mode matters here because the kit is authored on quarter turns: a
    // wall placed at 90 degrees scales and nudges along ITS axes, not the
    // world's, and world-only handles made that piece awkward to adjust. World
    // stays the default because the grid is the usual frame of reference, and a
    // multi-selection has no single local frame to speak of.
    const ImGuizmo::MODE mode =
        (mGizmoLocal && !multi) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    ImGuizmo::PushID("scene-selection");
    const bool manipulated = ImGuizmo::Manipulate(
        glm::value_ptr(view), glm::value_ptr(projection), operation, mode,
        glm::value_ptr(matrix), nullptr, snapPtr);
    const bool gizmoUsing = ImGuizmo::IsUsing();
    mGizmoHovered = ImGuizmo::GetHoveredHandleType() != ImGuizmo::MT_NONE;
    ImGuizmo::PopID();

    if (gizmoUsing && !mGizmoDragging) {
        // One capture at the start of the drag: the whole drag becomes a single
        // undo entry, not one per frame.
        mGizmoDragging = true;
        mDragAnchor = liveAnchor;
        mDragStart.clear();
        for (const AuthorId& id : mState.selection)
            if (const Entity* entity = mState.document.find(id))
                mDragStart.emplace_back(id, entity->transform);
    }
    if (gizmoUsing || manipulated)
        mDragGizmoMatrix = matrix;

    // Manipulate may report a final changed matrix on the same frame it clears
    // IsUsing after mouse release. Apply that matrix before closing undo state.
    if (manipulated && mGizmoDragging) {
        glm::vec3 position, ignoredRotation, scale;
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(matrix), glm::value_ptr(position),
            glm::value_ptr(ignoredRotation), glm::value_ptr(scale));
        const glm::vec3 rotation =
            authorRotationDegrees(orientationFromMatrix(matrix, scale));
        // The gizmo reports where the ANCHOR moved to; entities move by the
        // same delta rather than teleporting their origin to that anchor.
        const glm::vec3 delta = position - anchor;

        // The gizmo works in world space, because that is where the mouse is.
        // For an entity with no parent that IS its authored transform, which is
        // every entity in a flat scene; for a child it has to be pushed back
        // through the parent's frame before it is stored.
        const auto frameOf = [this](const AuthorId& id) {
            const Entity* entity = mState.document.find(id);
            return (entity && !entity->parent.empty())
                       ? mState.document.worldTransform(entity->parent)
                       : WorldTransform{};
        };
        const auto store = [](Entity& entity, const WorldTransform& frame,
                              const WorldTransform& world) {
            entity.transform = localFromWorld(frame, world);
        };

        if (!multi) {
            Entity* entity = mState.document.find(primary->id);
            if (entity && !mDragStart.empty()) {
                const XformAuthor& before = mDragStart.front().second;
                const WorldTransform frame = frameOf(primary->id);
                WorldTransform after = composeTransform(frame, before);
                after.position += delta;
                if (rotating)
                    after.orientation = authorOrientation(rotation);
                if (scaling)
                    after.scale = scale;
                store(*entity, frame, after);
            }
        }
        else {
            // Rebuild from drag-start state. Group scale is uniform because
            // non-uniform world scale cannot be represented faithfully as each
            // rotated member's local XformAuthor scale.
            const glm::quat spin = rotating ? authorOrientation(rotation)
                                            : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            const glm::vec3 groupScale = scaling ? scale : glm::vec3(1.0f);
            for (const auto& [id, before] : mDragStart) {
                Entity* entity = mState.document.find(id);
                if (!entity)
                    continue;
                // Composed to world first: a multi-selection can mix parented
                // and free entities, and turning the group about the anchor
                // only means anything in one shared frame.
                const WorldTransform frame = frameOf(id);
                const WorldTransform was = composeTransform(frame, before);
                const glm::vec3 offset = was.position - anchor;
                WorldTransform after;
                after.position = anchor + spin * (offset * groupScale) + delta;
                after.orientation = rotating ? spin * was.orientation
                                             : was.orientation;
                after.scale = scaling ? was.scale * groupScale : was.scale;
                store(*entity, frame, after);
            }
        }
        mState.document.touch();
    }

    if (!gizmoUsing && mGizmoDragging) {
        mGizmoDragging = false;
        std::vector<Command> parts;
        for (const auto& [id, before] : mDragStart) {
            const Entity* entity = mState.document.find(id);
            if (!entity)
                continue;
            if (entity->transform.position == before.position &&
                entity->transform.rotationDegrees == before.rotationDegrees &&
                entity->transform.scale == before.scale)
                continue; // a click that moved nothing is not an edit
            parts.push_back(makeSetTransform(id, before, entity->transform));
        }
        if (!parts.empty()) {
            // The document already holds the final value, so the composite is
            // applied to a state it is idempotent on.
            runCommand(makeComposite("move selection", std::move(parts)));
        }
        mDragStart.clear();
    }

    // Selection outline: a box around what the gizmo is acting on, so the
    // handles are never ambiguous about their subject.
    if (haveBounds) {
        const glm::mat4 viewProjection = projection * view;
        const auto project = [&](const glm::vec3& world, ImVec2& out) {
            glm::vec2 screen;
            if (!projectToViewport(world, viewProjection,
                                   {mViewportX, mViewportY},
                                   {mViewportW, mViewportH}, screen))
                return false;
            out = ImVec2(screen.x, screen.y);
            return true;
        };
        const glm::vec3 corners[8] = {
            {boundsMin.x, boundsMin.y, boundsMin.z},
            {boundsMax.x, boundsMin.y, boundsMin.z},
            {boundsMax.x, boundsMin.y, boundsMax.z},
            {boundsMin.x, boundsMin.y, boundsMax.z},
            {boundsMin.x, boundsMax.y, boundsMin.z},
            {boundsMax.x, boundsMax.y, boundsMin.z},
            {boundsMax.x, boundsMax.y, boundsMax.z},
            {boundsMin.x, boundsMax.y, boundsMax.z},
        };
        static constexpr int kBoxEdges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
            {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 colour = IM_COL32(255, 190, 60, 200);
        for (const auto& edge : kBoxEdges) {
            ImVec2 a, b;
            if (project(corners[edge[0]], a) && project(corners[edge[1]], b))
                draw->AddLine(a, b, colour, 1.5f);
        }
    }
}

// --- placement ---------------------------------------------------------------

// Where the cursor is pointing, expressed as a grid placement. Kit pieces with
// a socket snap to a cell (floor/fill) or a cell edge (wall/opening); anything
// else lands freely on the work plane, snapped to the current subdivision.
Ray EditorApp::mouseRay() const
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    return viewportRay({mouse.x, mouse.y}, {mViewportX, mViewportY},
                       {mViewportW, mViewportH}, mState.camera.activeEye(),
                       mState.camera.activeOrientation(),
                       glm::radians(mState.camera.activeFovDeg()));
}

bool EditorApp::hoveredPlacement(CellPlacement& cell,
                                 XformAuthor& transform) const
{
    if (mState.brushPrefab.empty() || mViewportW < 8.0f)
        return false;
    const KitPiece* piece = mState.catalog.find(mState.brushPrefab);
    if (!piece)
        return false;

    // Always resolves to a point: a ghost that vanishes whenever the camera
    // tips towards the horizon is the single most confusing thing about a
    // placement tool, and the ray misses the work plane constantly.
    const Ray ray = mouseRay();
    glm::vec3 hit = workPlanePoint(ray, mState.gridState.level,
                                   kGhostFallbackDistance);

    cell = CellPlacement{};
    cell.level = mState.gridState.level;
    cell.span = piece->span;
    cell.yawQuarters = mBrushYawQuarters;

    if (socketUsesGrid(piece->socket)) {
        if (piece->socket == Socket::Wall || piece->socket == Socket::Opening) {
            // Snapped to the nearest grid LINE, so the ghost stays put along
            // the length of a wall instead of flipping edges mid-stroke.
            nearestWallSlot(mState.grid, hit, cell.col, cell.row, cell.edge);
        }
        else {
            pointToCell(mState.grid, hit, cell.col, cell.row);
        }
        transform =
            placementToTransform(mState.grid, mState.catalog, *piece, cell);
    }
    else {
        // Props are free, so the grid subdivision is only an aid here.
        if (mState.gridState.snap) {
            const float step = mState.gridState.step();
            hit.x = std::round(hit.x / step) * step;
            hit.z = std::round(hit.z / step) * step;
        }
        transform = XformAuthor{};
        transform.position = hit;
        transform.position.y += piece->yOffsetMeters(mState.catalog.scale());
        transform.rotationDegrees.y = float(mBrushYawQuarters) * 90.0f;
    }
    return true;
}

void EditorApp::placeAt(const CellPlacement& cell, const XformAuthor& transform)
{
    const KitPiece* piece = mState.catalog.find(mState.brushPrefab);
    if (!piece)
        return;

    // One piece per slot per stroke: a stroke places every frame the button is
    // held, so without this a click that lasts a second drops sixty props.
    // Grid pieces key on their cell; a free prop keys on where it landed.
    const std::string slot =
        socketUsesGrid(piece->socket)
            ? gridPaintSlot(cell)
            : freePaintSlot(transform.position, mState.gridState.snap
                                                    ? mState.gridState.step()
                                                    : kFreePaintSpacing);
    for (const std::string& painted : mPaintedSlots)
        if (painted == slot)
            return;
    mPaintedSlots.push_back(slot);

    Entity entity;
    entity.id = mState.document.allocateId(mState.brushPrefab);
    entity.name = entity.id;
    entity.prefab = mState.brushPrefab;
    entity.transform = transform;
    if (socketUsesGrid(piece->socket))
        entity.cell = cell;

    // Applied immediately so the next hover sees it; the command is recorded
    // and pushed as one composite when the drag ends.
    mState.document.add(entity);
    mPaintedIds.push_back(entity.id);
    mPaintParts.push_back(makeCreateEntity(entity));
}

void EditorApp::drawPlacementGhost()
{
    if (mState.tool != Tool::Place || !mViewportHovered || mFlying) {
        mPreview->hidePlacementGhost();
        return;
    }

    CellPlacement cell;
    XformAuthor transform;
    if (!hoveredPlacement(cell, transform)) {
        mPreview->hidePlacementGhost();
        return;
    }
    const KitPiece* piece = mState.catalog.find(mState.brushPrefab);
    if (!piece) {
        mPreview->hidePlacementGhost();
        return;
    }

    mPreview->showPlacementGhost(*piece, transform, mState.catalog.scale());

    // The ghost is a wire box in screen space rather than a translucent mesh:
    // it needs no material, cannot be picked, and reads clearly against any
    // geometry behind it.
    glm::vec3 localMin, localMax;
    piece->localBoundsMeters(mState.catalog.scale(), localMin, localMax);
    const glm::mat4 worldMatrix = authorTransformMatrix(transform);

    glm::mat4 view, projection;
    cameraMatrices(mState.camera, mViewportW / mViewportH, view, projection);
    const glm::mat4 viewProjection = projection * view;

    const auto project = [&](const glm::vec3& local, ImVec2& out) {
        const glm::vec3 world = glm::vec3(worldMatrix * glm::vec4(local, 1.0f));
        glm::vec2 screen;
        if (!projectToViewport(world, viewProjection, {mViewportX, mViewportY},
                               {mViewportW, mViewportH}, screen))
            return false;
        out = ImVec2(screen.x, screen.y);
        return true;
    };

    const glm::vec3 corners[8] = {
        {localMin.x, localMin.y, localMin.z},
        {localMax.x, localMin.y, localMin.z},
        {localMax.x, localMin.y, localMax.z},
        {localMin.x, localMin.y, localMax.z},
        {localMin.x, localMax.y, localMin.z},
        {localMax.x, localMax.y, localMin.z},
        {localMax.x, localMax.y, localMax.z},
        {localMin.x, localMax.y, localMax.z},
    };
    static constexpr int kEdges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0},
                                          {4, 5}, {5, 6}, {6, 7}, {7, 4},
                                          {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 colour = IM_COL32(120, 220, 160, 200);
    for (const auto& edge : kEdges) {
        ImVec2 a, b;
        if (project(corners[edge[0]], a) && project(corners[edge[1]], b))
            draw->AddLine(a, b, colour, 1.5f);
    }
}

// The staging gizmo. Deliberately not the scene gizmo: it drives a render node
// directly rather than a document entity, because nothing here is authored and
// nothing here belongs in an undo history.
void EditorApp::drawStageGizmo(const eng::FrameContext& f)
{
    if (!mStage.built() || mStage.previewMode() != StagePreview::Sphere ||
        mViewportW < 8.0f)
        return;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(mViewportX, mViewportY, mViewportW, mViewportH);

    glm::mat4 view, projection;
    cameraMatrices(mState.camera, mViewportW / mViewportH, view, projection);

    // Rebuilt from the spin each frame: the turntable owns the rotation, and
    // the gizmo hands it back a new angle rather than a whole transform.
    glm::mat4 matrix(1.0f);
    // On the subject, not at the world origin: a rotate handle floating under
    // the thing it rotates is just confusing.
    const glm::vec3 position = mStage.focusPoint();
    const glm::vec3 rotation{0.0f, glm::degrees(mStage.spin()), 0.0f};
    const glm::vec3 scale{1.0f, 1.0f, 1.0f};
    ImGuizmo::RecomposeMatrixFromComponents(
        glm::value_ptr(position), glm::value_ptr(rotation),
        glm::value_ptr(scale), glm::value_ptr(matrix));

    // Rotate only: translating or scaling the subject would break the framing
    // the reference floor and the three-point rig were set up for.
    ImGuizmo::PushID("material-stage");
    const bool manipulated = ImGuizmo::Manipulate(
        glm::value_ptr(view), glm::value_ptr(projection), ImGuizmo::ROTATE_Y,
        ImGuizmo::WORLD, glm::value_ptr(matrix));
    const bool gizmoUsing = ImGuizmo::IsUsing();
    ImGuizmo::PopID();
    if (gizmoUsing || manipulated)
        mStageAutoSpin = false; // dragging takes over from the turntable
    if (manipulated) {
        const float yaw = std::atan2(-matrix[0][2], matrix[0][0]);
        mStage.setSpin(f.engine.renderer(), yaw);
    }
}

// --- room tool ---------------------------------------------------------------

bool EditorApp::hoveredCell(int& col, int& row) const
{
    if (mViewportW < 8.0f)
        return false;
    const Ray ray = mouseRay();
    glm::vec3 hit;
    if (!rayPlaneY(ray, mState.gridState.level, hit))
        return false;
    pointToCell(mState.grid, hit, col, row);
    return true;
}

void EditorApp::drawRoomPreview(const eng::FrameContext& f)
{
    int col = 0, row = 0;
    if (!hoveredCell(col, row))
        return;

    // While dragging, the rectangle runs from where the drag began; before it,
    // a single cell under the cursor shows where the room would start.
    const int c0 = mRoomDragging ? mRoomStartCol : col;
    const int r0 = mRoomDragging ? mRoomStartRow : row;
    RoomSpec spec = mState.roomSpec;
    spec.col0 = c0;
    spec.row0 = r0;
    spec.col1 = col;
    spec.row1 = row;
    spec.level = mState.gridState.level;

    glm::mat4 view, projection;
    cameraMatrices(mState.camera, mViewportW / mViewportH, view, projection);
    const glm::mat4 viewProjection = projection * view;
    const auto project = [&](const glm::vec3& world, ImVec2& out) {
        glm::vec2 screen;
        if (!projectToViewport(world, viewProjection, {mViewportX, mViewportY},
                               {mViewportW, mViewportH}, screen))
            return false;
        out = ImVec2(screen.x, screen.y);
        return true;
    };

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float cell = mState.grid.cell;
    const float level = spec.level;
    const glm::vec3 min =
        cellCentre(mState.grid, spec.minCol(), spec.minRow(), level) -
        glm::vec3(cell * 0.5f, 0.0f, cell * 0.5f);
    const glm::vec3 max =
        cellCentre(mState.grid, spec.maxCol(), spec.maxRow(), level) +
        glm::vec3(cell * 0.5f, 0.0f, cell * 0.5f);

    // Filled footprint, so the room reads as an area rather than an outline.
    ImVec2 quad[4];
    const glm::vec3 floorCorners[4] = {{min.x, level, min.z},
                                       {max.x, level, min.z},
                                       {max.x, level, max.z},
                                       {min.x, level, max.z}};
    bool visible = true;
    for (int i = 0; i < 4; ++i)
        visible = visible && project(floorCorners[i], quad[i]);
    if (visible) {
        draw->AddConvexPolyFilled(quad, 4, IM_COL32(120, 200, 255, 40));
        draw->AddPolyline(quad, 4, IM_COL32(140, 220, 255, 220),
                          ImDrawFlags_Closed, 2.0f);
        // Cell divisions inside the footprint: the author can count cells and
        // know the room's size before committing to it.
        for (int col2 = spec.minCol() + 1; col2 <= spec.maxCol(); ++col2) {
            const float x = min.x + float(col2 - spec.minCol()) * cell;
            ImVec2 a, b;
            if (project({x, level, min.z}, a) && project({x, level, max.z}, b))
                draw->AddLine(a, b, IM_COL32(140, 220, 255, 70));
        }
        for (int row2 = spec.minRow() + 1; row2 <= spec.maxRow(); ++row2) {
            const float z = min.z + float(row2 - spec.minRow()) * cell;
            ImVec2 a, b;
            if (project({min.x, level, z}, a) && project({max.x, level, z}, b))
                draw->AddLine(a, b, IM_COL32(140, 220, 255, 70));
        }
        // Wall height, drawn at the corners, so a room is not mistaken for a
        // floor patch.
        if (const KitPiece* wall =
                mState.catalog.find(mState.roomSpec.wallPrefab)) {
            const float height = wall->sizeMeters(mState.catalog.scale()).y;
            for (int i = 0; i < 4; ++i) {
                ImVec2 top;
                glm::vec3 up = floorCorners[i];
                up.y += height;
                if (project(up, top))
                    draw->AddLine(quad[i], top, IM_COL32(140, 220, 255, 140),
                                  1.5f);
            }
        }
    }

    // Size readout at the cursor: cells and metres, because a level is authored
    // in cells and played in metres.
    const std::string label =
        std::to_string(spec.width()) + " x " + std::to_string(spec.depth()) +
        " cells  (" + std::to_string(int(float(spec.width()) * cell)) + " x " +
        std::to_string(int(float(spec.depth()) * cell)) + " m)";
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    draw->AddText(ImVec2(mouse.x + 16.0f, mouse.y + 8.0f),
                  IM_COL32(230, 240, 255, 255), label.c_str());
}

void EditorApp::commitRoom()
{
    int col = 0, row = 0;
    if (!hoveredCell(col, row))
        return;
    RoomSpec spec = mState.roomSpec;
    spec.col0 = mRoomStartCol;
    spec.row0 = mRoomStartRow;
    spec.col1 = col;
    spec.row1 = row;
    spec.level = mState.gridState.level;

    std::string error;
    const std::vector<Entity> pieces =
        buildRoom(mState.grid, mState.catalog, spec, mState.document, error);
    if (!error.empty()) {
        mStatus = error;
        return;
    }
    if (pieces.empty())
        return;

    std::vector<Command> parts;
    parts.reserve(pieces.size());
    for (const Entity& piece : pieces)
        parts.push_back(makeCreateEntity(piece));
    runCommand(makeComposite("build " + std::to_string(spec.width()) + "x" +
                                 std::to_string(spec.depth()) + " room",
                             std::move(parts)));
    mPreview->invalidate();
    mStatus = "built a " + std::to_string(spec.width()) + " x " +
              std::to_string(spec.depth()) + " room (" +
              std::to_string(pieces.size()) + " pieces)";
}

const OutlinerTree& EditorApp::outlinerTree()
{
    OutlinerOptions options;
    options.filter = mOutlinerFilter;
    options.showGeometry = mOutlinerShowGeometry;
    // Grouping walks and sorts every entity, and this panel is open while the
    // gizmo is dragged -- so it is rebuilt on a document revision or an option
    // change, never per frame.
    if (mOutlinerRevision != mState.document.revision ||
        options.filter != mOutlinerOptions.filter ||
        options.showGeometry != mOutlinerOptions.showGeometry) {
        mOutlinerRevision = mState.document.revision;
        mOutlinerOptions = options;
        mOutliner = buildOutliner(mState.document, mState.catalog, options);
    }
    return mOutliner;
}

void EditorApp::selectGroup(const OutlinerGroup& group, bool add)
{
    if (!add)
        mState.selection.clear();
    // The whole group, descendants included: selecting a composed object has to
    // reach past its root, or dragging the chandelier leaves the candles.
    for (const AuthorId& id : groupIds(group))
        if (!mState.isSelected(id))
            mState.selection.push_back(id);
}

// The edit that re-hangs one entity, without running it.
//
// Returned rather than applied so a batch closes as ONE undo entry: parenting
// eight props to a group and then needing eight Ctrl+Z to take it back is a
// history nobody can use, and it is what a per-call runCommand produced.
bool EditorApp::buildReparent(const AuthorId& child, const AuthorId& parent,
                              Command& out)
{
    const Entity* source = mState.document.find(child);
    if (!source || source->parent == parent)
        return false;
    if (!parent.empty() && !mState.document.contains(parent)) {
        mStatus = "no entity called '" + parent + "'";
        return false;
    }
    // Refused rather than allowed and reported: a cycle makes the outliner
    // unwalkable, and the author would be fixing it from a panel that can no
    // longer draw the thing they broke.
    if (mState.document.wouldCycle(child, parent)) {
        mStatus = "cannot parent '" + child + "' to something below it";
        return false;
    }

    // The entity keeps the place it is drawn in. Its transform is local to its
    // parent, so re-hanging it under a different one has to re-express that
    // offset -- otherwise every drag in the outliner teleports something.
    const WorldTransform world = mState.document.worldTransform(child);
    const WorldTransform frame =
        parent.empty() ? WorldTransform{} : mState.document.worldTransform(parent);
    Entity updated = *source;
    updated.parent = parent;
    updated.transform = localFromWorld(frame, world);
    // A cell placement is addressed in the grid's frame, not a parent's, so a
    // parented piece is no longer grid-constrained. Keeping the cell would let
    // the cooker and the viewport disagree about where the piece is.
    if (!parent.empty())
        updated.cell.reset();

    out = makeEditEntity(parent.empty()
                             ? "detach " + child
                             : "parent " + child + " to " + parent,
                         child, *source, updated);
    return true;
}

void EditorApp::reparentEntity(const AuthorId& child, const AuthorId& parent)
{
    Command command;
    if (!buildReparent(child, parent, command))
        return;
    runCommand(std::move(command));
    mPreview->invalidate();
    mStatus = parent.empty() ? child + " detached"
                             : child + " parented to " + parent;
}

// "These are one thing now." The first selected entity becomes the parent,
// because that is the one the author clicked first and the only choice a
// multi-selection carries any information about.
void EditorApp::parentSelectionToPrimary()
{
    if (mState.selection.size() < 2)
        return;
    const AuthorId root = mState.selection.front();
    // Built against the document as it stands, then run together. Each edit is
    // independent -- they all re-express one child against one unchanged parent
    // -- so building them all first changes nothing but the undo granularity.
    const std::vector<AuthorId> children(mState.selection.begin() + 1,
                                         mState.selection.end());
    std::vector<Command> parts;
    for (const AuthorId& child : children) {
        Command command;
        if (buildReparent(child, root, command))
            parts.push_back(std::move(command));
    }
    if (parts.empty())
        return;

    const std::size_t count = parts.size();
    runCommand(makeComposite("parent " + std::to_string(count) + " to " + root,
                             std::move(parts)));
    mPreview->invalidate();
    mStatus = "parented " + std::to_string(count) + " to " + root;
}

void EditorApp::detachSelection()
{
    std::vector<Command> parts;
    for (const AuthorId& id : mState.selection) {
        Command command;
        if (buildReparent(id, {}, command))
            parts.push_back(std::move(command));
    }
    if (parts.empty()) {
        mStatus = "nothing to detach";
        return;
    }
    const std::size_t count = parts.size();
    runCommand(
        makeComposite("detach " + std::to_string(count), std::move(parts)));
    mPreview->invalidate();
    mStatus = std::to_string(count) + " detached";
}

// Hide everything the selection does not need.
//
// The move an author makes constantly in a dressed room and cannot make here:
// "just this, and what it is attached to". Undone by the show-everything
// button, which is why this does not have to be an undoable command -- nothing
// about the document changes.
void EditorApp::isolateSelection()
{
    if (mState.selection.empty())
        return;
    const std::vector<AuthorId> keep =
        withDescendants(mState.document, mState.selection);

    mState.hidden.clear();
    for (const Entity& entity : mState.document.entities) {
        if (std::find(keep.begin(), keep.end(), entity.id) == keep.end())
            mState.hidden.push_back(entity.id);
    }
    mPreview->invalidate();
    mStatus = "isolated " + std::to_string(keep.size()) + " of " +
              std::to_string(mState.document.entities.size());
}

// Brings a docked panel forward, for the frames after startup during which a
// restored layout is still re-selecting its own saved tabs. Verification hook
// (PSX_EDITOR_PANEL); does nothing in an ordinary session.
void EditorApp::focusPanelIfRequested(const char* name)
{
    if (mFocusPanel.empty() || mFocusPanelFrames <= 0 || mFocusPanel != name)
        return;
    ImGui::SetNextWindowFocus();
}

// Selecting from anywhere but the outliner: the panel has to follow.
//
// Picking in the viewport and then hunting the row in a three-hundred-row list
// is the single most common thing an editor makes people do twice. Reveal is
// consumed by the next draw, so this is a request, not a mode.
void EditorApp::selectAndReveal(const AuthorId& id, bool toggle)
{
    if (toggle)
        mState.toggleSelected(id);
    else
        mState.select(id);
    mSelectionAnchor = id;
    mOutlinerReveal = id;
}

// The 2D viewport.
//
// Everything below decides *what state* the HUD is drawn against and *where*
// on screen; the drawing itself is `game::GameHud`, the class the game runs.
// A HUD preview that reimplements the HUD tells you about the preview.
void EditorApp::drawUiStage()
{
    focusPanelIfRequested("ui");
    if (!ImGui::Begin("UI", nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }
    if (!mUiHudReady)
        mUiHudReady = mUiHud.initialise();
    if (!mUiHudReady) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "the HUD font atlas did not load");
        ImGui::End();
        return;
    }

    // --- the surface ------------------------------------------------------
    ImGui::SetNextItemWidth(150.0f);
    const char* kResolutions[] = {"320 x 240", "384 x 216", "640 x 360",
                                  "640 x 480", "fit the panel"};
    static const glm::ivec2 kSizes[] = {
        {320, 240}, {384, 216}, {640, 360}, {640, 480}, {0, 0}};
    int chosen = int(std::size(kSizes)) - 1;
    for (int i = 0; i < int(std::size(kSizes)); ++i)
        if (kSizes[i] == mUiStage.virtualSize)
            chosen = i;
    if (ImGui::Combo("##uires", &chosen, kResolutions,
                     int(std::size(kResolutions))))
        mUiStage.virtualSize = kSizes[chosen];
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("the virtual resolution the layout is drawn at\n"
                          "the game picks this from the window; the failures\n"
                          "are at the extremes, and a window is never extreme");
    ImGui::SameLine();
    ImGui::Checkbox("safe area", &mUiStage.showSafeArea);
    ImGui::SameLine();
    ImGui::Checkbox("grid", &mUiStage.showGrid);

    // --- the canvas -------------------------------------------------------
    // Drawn before the controls and given every pixel that is left: this is a
    // viewport, and a viewport that a row of sliders can squeeze to nothing is
    // a property sheet with a picture on it. The controls sit under it in a
    // fixed strip, which also means they never move as the panel resizes.
    constexpr float kControlStrip = 132.0f;
    const ImVec2 room = ImGui::GetContentRegionAvail();
    const ImVec2 available(room.x, std::max(room.y - kControlStrip, 48.0f));
    if (available.x < 32.0f || available.y < 32.0f) {
        ImGui::TextDisabled("not enough room");
        ImGui::End();
        return;
    }
    // "fit the panel" is a virtual size derived from the room there is, at
    // scale 1: the only honest way to preview a resolution nobody chose.
    glm::ivec2 virtualSize = mUiStage.virtualSize;
    if (virtualSize.x <= 0 || virtualSize.y <= 0)
        virtualSize = {int(available.x), int(available.y)};
    const int scale = fitScale(virtualSize, {available.x, available.y});
    mUiStage.scale = scale;

    const ImVec2 extent(float(virtualSize.x * scale),
                        float(virtualSize.y * scale));
    // Centred in the room, the way any viewport centres its subject.
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 origin(cursor.x + std::max((available.x - extent.x) * 0.5f, 0.0f),
                        cursor.y + std::max((available.y - extent.y) * 0.5f, 0.0f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // A black plate under it: the HUD is drawn over a game, and judging its
    // contrast against the editor's grey chrome is judging the wrong thing.
    draw->AddRectFilled(origin,
                        ImVec2(origin.x + extent.x, origin.y + extent.y),
                        IM_COL32(8, 9, 12, 255));

    if (mUiStage.showGrid) {
        // Every 16 virtual pixels: the layout's own unit, so a widget that is
        // one pixel off its column is visible rather than merely wrong.
        const ImU32 line = IM_COL32(255, 255, 255, 18);
        for (int x = 16; x < virtualSize.x; x += 16)
            draw->AddLine(ImVec2(origin.x + float(x * scale), origin.y),
                          ImVec2(origin.x + float(x * scale),
                                 origin.y + extent.y),
                          line);
        for (int y = 16; y < virtualSize.y; y += 16)
            draw->AddLine(ImVec2(origin.x, origin.y + float(y * scale)),
                          ImVec2(origin.x + extent.x,
                                 origin.y + float(y * scale)),
                          line);
    }

    // The real HUD, on the real canvas, into this panel's draw list.
    mUiHud.drawInto(hudSnapshotFrom(mUiStage), hudTooltipFrom(mUiStage),
                    mUiStage.dt, {origin.x, origin.y}, virtualSize, scale, draw);

    if (mUiStage.showSafeArea) {
        // A console HUD that ignores the safe area is legible on a monitor and
        // cropped on a television, and the crop is not something the developer
        // ever sees.
        const float inset = float(mUiStage.safeAreaPercent) / 100.0f;
        const ImVec2 a(origin.x + extent.x * inset,
                       origin.y + extent.y * inset);
        const ImVec2 b(origin.x + extent.x * (1.0f - inset),
                       origin.y + extent.y * (1.0f - inset));
        draw->AddRect(a, b, IM_COL32(240, 200, 80, 110));
    }
    // The frame last, so it is never drawn over.
    draw->AddRect(origin, ImVec2(origin.x + extent.x, origin.y + extent.y),
                  IM_COL32(120, 140, 160, 160));
    ImGui::Dummy(available);

    ImGui::TextDisabled("%d x %d virtual   x%d   %.0f x %.0f px",
                        virtualSize.x, virtualSize.y, scale, double(extent.x),
                        double(extent.y));

    // --- the state --------------------------------------------------------
    // Two columns of sliders in the strip below, because the failures worth
    // previewing are combinations: a long weapon name *and* three statuses *and*
    // a tooltip, at 320x240.
    if (ImGui::BeginTable("##uistate", 2, ImGuiTableFlags_SizingStretchSame)) {
        // The reserved strip fits the longest label in each column, measured
        // rather than guessed: a label clipped to "disciplir" is the panel
        // telling the author it ran out of room, which is not what happened.
        const float leftLabels = ImGui::CalcTextSize("statuses").x + 12.0f;
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-leftLabels);
        ImGui::SliderFloat("health", &mUiStage.health, 0.0f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(-leftLabels);
        ImGui::SliderFloat("stamina", &mUiStage.stamina, 0.0f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(-leftLabels);
        ImGui::SliderFloat("arcana", &mUiStage.mana, 0.0f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(-leftLabels);
        ImGui::SliderInt("statuses", &mUiStage.statusCount, 0,
                         game::HudSnapshot::kMaxStatuses);

        const float rightLabels = ImGui::CalcTextSize("discipline").x + 12.0f;
        ImGui::TableNextColumn();
        char weapon[64];
        std::snprintf(weapon, sizeof(weapon), "%s", mUiStage.weaponName.c_str());
        ImGui::SetNextItemWidth(-rightLabels);
        if (ImGui::InputText("weapon", weapon, sizeof(weapon)))
            mUiStage.weaponName = weapon;
        char discipline[64];
        std::snprintf(discipline, sizeof(discipline), "%s",
                      mUiStage.weaponDiscipline.c_str());
        ImGui::SetNextItemWidth(-rightLabels);
        if (ImGui::InputText("discipline", discipline, sizeof(discipline)))
            mUiStage.weaponDiscipline = discipline;
        ImGui::Checkbox("tooltip", &mUiStage.showTooltip);
        if (mUiStage.showTooltip) {
            char title[64];
            std::snprintf(title, sizeof(title), "%s",
                          mUiStage.tooltipTitle.c_str());
            ImGui::SetNextItemWidth(-rightLabels);
            if (ImGui::InputText("title", title, sizeof(title)))
                mUiStage.tooltipTitle = title;
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void EditorApp::drawOutliner()
{
    focusPanelIfRequested("outliner");
    if (!ImGui::Begin("Outliner", nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##outlinerfilter", "search  (has: kind:)",
                             mOutlinerFilter, sizeof(mOutlinerFilter));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "free text matches name, id, kind and prefab\n"
            "has:collider   entities carrying that component\n"
            "kind:enemy     entities of exactly that kind\n"
            "terms are AND-ed: has:trigger kind:wall");

    // A row of icon switches rather than a line of checkboxes and words: this
    // panel docks narrow, and the header was taking two lines of it to say
    // things a shape says in one.
    if (iconButton(Icon::Cube, "##geometry",
                   mOutlinerShowGeometry
                       ? "kit geometry shown -- click to fold it away"
                       : "kit geometry hidden from the list",
                   mOutlinerShowGeometry))
        mOutlinerShowGeometry = !mOutlinerShowGeometry;
    ImGui::SameLine();

    const bool anyHidden = !mState.hidden.empty();
    if (iconButton(Icon::Eye, "##showall",
                   anyHidden ? "show everything that is hidden"
                             : "nothing is hidden",
                   anyHidden) &&
        anyHidden) {
        mState.hidden.clear();
        mPreview->invalidate();
    }
    ImGui::SameLine();

    const bool anyLocked = !mState.locked.empty();
    if (iconButton(Icon::Unlock, "##unlockall",
                   anyLocked ? "unlock everything" : "nothing is locked",
                   anyLocked) &&
        anyLocked)
        mState.locked.clear();
    ImGui::SameLine();

    // Isolate: the one selection verb that is missing from every list panel
    // until somebody adds it, and the one that makes a dense room workable.
    const bool canIsolate = !mState.selection.empty();
    if (iconButton(Icon::EyeClosed, "##isolate",
                   "isolate the selection -- hide everything else", canIsolate) &&
        canIsolate)
        isolateSelection();

    ImGui::SameLine();
    const OutlinerTree& tree = outlinerTree();
    ImGui::TextDisabled("%zu / %zu", tree.shown,
                        mState.document.entities.size());
    if (anyHidden) {
        ImGui::SameLine();
        ImGui::TextDisabled("| %zu hidden", mState.hidden.size());
    }

    ImGui::Separator();
    if (ImGui::BeginChild("##entities")) {
        // The rows themselves live in OutlinerPanel.cpp, so a headless ImGui
        // test can click them; this only says what a click should do.
        OutlinerActions actions;
        actions.isSelected = [this](const AuthorId& id) {
            return mState.isSelected(id);
        };
        actions.selectGroup = [this](const OutlinerGroup& group, bool add) {
            selectGroup(group, add);
        };
        actions.selectNode = [this](const AuthorId& id, bool add) {
            if (add)
                mState.toggleSelected(id);
            else
                mState.select(id);
            mSelectionAnchor = id;
        };
        // Ctrl toggles, Shift takes the run between the anchor and here, plain
        // click replaces and becomes the new anchor. The order comes from the
        // panel because only the panel knows what is drawn and what is folded
        // away inside a collapsed group.
        actions.clickNode = [this](const AuthorId& id, SelectMode mode,
                                   const OutlinerRowOrder& rows) {
            switch (mode) {
            case SelectMode::Replace:
                mState.select(id);
                mSelectionAnchor = id;
                break;
            case SelectMode::Toggle:
                mState.toggleSelected(id);
                mSelectionAnchor = id;
                break;
            case SelectMode::Range: {
                // With no anchor -- first click of a session, or the anchor's
                // row is gone -- a range is just a click. Selecting from the
                // top of the list instead would be a surprise measured in
                // hundreds of entities.
                const std::vector<AuthorId> run =
                    mSelectionAnchor.empty()
                        ? std::vector<AuthorId>{}
                        : rows.between(mSelectionAnchor, id);
                if (run.empty()) {
                    mState.select(id);
                    mSelectionAnchor = id;
                    break;
                }
                // Replaces rather than extends: Shift-clicking twice from one
                // anchor should give the second range, not their union, which
                // is how every list behaves and the only way to shrink a range
                // once it overshoots. The anchor stays put for exactly that.
                mState.selection.clear();
                for (const AuthorId& row : run)
                    mState.selection.push_back(row);
                break;
            }
            }
        };
        actions.focus = [this] { frameSelectionOrAll(); };
        actions.contextMenu = [this] { drawSelectionContextMenu(); };
        actions.reparent = [this](const AuthorId& child, const AuthorId& parent) {
            reparentEntity(child, parent);
        };
        actions.isHidden = [this](const AuthorId& id) {
            return mState.isHidden(id);
        };
        actions.setHidden = [this](const AuthorId& id, bool on) {
            // Descendants follow: hiding a chandelier and leaving its candles
            // floating is not hiding it.
            mState.setHidden(id, on);
            for (const AuthorId& below : mState.document.descendantsOf(id))
                mState.setHidden(below, on);
            mPreview->invalidate();
        };
        actions.isLocked = [this](const AuthorId& id) {
            return mState.isLocked(id);
        };
        actions.setLocked = [this](const AuthorId& id, bool on) {
            mState.setLocked(id, on);
            for (const AuthorId& below : mState.document.descendantsOf(id))
                mState.setLocked(below, on);
            if (on)
                mState.selection.erase(
                    std::remove(mState.selection.begin(),
                                mState.selection.end(), id),
                    mState.selection.end());
        };
        // Reveal whatever the world selected while the panel was not looking.
        actions.reveal = mOutlinerReveal;
        drawOutlinerRows(tree, !mOutlinerOptions.filter.empty(), actions,
                         mOutlinerRows);
        mOutlinerReveal.clear();
    }
    ImGui::EndChild();
    ImGui::End();
}

// Shared by both outliner rows: whatever is selected, act on all of it. Adding
// a component here is the same call the inspector makes, so there is one path
// from "author wants a collider" to the document.
void EditorApp::drawSelectionContextMenu()
{
    if (ImGui::MenuItem("Focus", "F"))
        frameSelectionOrAll();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        duplicateSelection();

    // Parenting from the menu as well as by dragging: a drag is the fast path
    // once you know it exists, and it is not a gesture anyone discovers.
    const bool canGroup = mState.selection.size() > 1;
    if (ImGui::MenuItem("Parent to first selected", nullptr, false, canGroup))
        parentSelectionToPrimary();
    bool anyParented = false;
    for (const AuthorId& id : mState.selection)
        if (const Entity* entity = mState.document.find(id))
            anyParented = anyParented || !entity->parent.empty();
    if (ImGui::MenuItem("Detach from parent", nullptr, false, anyParented))
        detachSelection();
    if (ImGui::BeginMenu("Add Component")) {
        const ComponentDefaults defaults = componentDefaults();
        for (const ComponentType& type : componentTypes()) {
            if (!type.add)
                continue;
            const bool ready = !type.addable || type.addable(defaults);
            if (ImGui::MenuItem(type.label, nullptr, false, ready))
                addComponentToSelection(type);
            if (!ready && ImGui::IsItemHovered())
                ImGui::SetTooltip("pick a piece in the Catalog first");
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Del"))
        deleteSelection();
}

ComponentDefaults EditorApp::componentDefaults() const
{
    ComponentDefaults defaults;
    defaults.prefab = mState.brushPrefab;
    return defaults;
}

// One undo entry for the whole selection: a component added to forty pillars
// has to come back off them in one Ctrl+Z, or the history is unusable.
void EditorApp::addComponentToSelection(const ComponentType& type)
{
    const ComponentDefaults defaults = componentDefaults();
    if (type.addable && !type.addable(defaults))
        return;
    std::vector<Command> parts;
    for (const AuthorId& id : mState.selection) {
        const Entity* entity = mState.document.find(id);
        if (!entity || type.has(*entity))
            continue;
        Entity after = *entity;
        type.add(after, defaults);
        parts.push_back(
            makeEditEntity("add " + std::string(type.id), id, *entity, after));
    }
    if (parts.empty())
        return;
    runCommand(
        makeComposite(std::string("add ") + type.label, std::move(parts)));
    mPreview->invalidate();
    mStatus = std::string("added ") + type.label;
}

void EditorApp::removeComponentFromSelection(const ComponentType& type)
{
    if (!type.remove)
        return;
    std::vector<Command> parts;
    for (const AuthorId& id : mState.selection) {
        const Entity* entity = mState.document.find(id);
        if (!entity || !type.has(*entity))
            continue;
        Entity after = *entity;
        type.remove(after);
        parts.push_back(makeEditEntity("remove " + std::string(type.id), id,
                                       *entity, after));
    }
    if (parts.empty())
        return;
    runCommand(
        makeComposite(std::string("remove ") + type.label, std::move(parts)));
    mPreview->invalidate();
    mStatus = std::string("removed ") + type.label;
}

// A point in front of the camera on the work plane: where a new gameplay
// entity should appear. Dropping them at the origin instead means the author
// has to go find every one of them.
glm::vec3 EditorApp::viewFocusPoint() const
{
    Ray ray;
    ray.origin = mState.camera.activeEye();
    ray.dir = mState.camera.activeOrientation() * glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 hit;
    if (rayPlaneY(ray, mState.gridState.level, hit))
        return hit;
    return ray.origin + ray.dir * 8.0f;
}

void EditorApp::addGameplayEntity(Gameplay kind)
{
    Entity entity;
    entity.transform.position = viewFocusPoint();
    if (mState.gridState.snap) {
        const float step = mState.gridState.step();
        entity.transform.position.x =
            std::round(entity.transform.position.x / step) * step;
        entity.transform.position.z =
            std::round(entity.transform.position.z / step) * step;
    }

    // The component's own defaults come from the registry, so "New > Light" and
    // "Add Component > Light" produce the same thing. Only the id stem and the
    // handful of per-menu tweaks live here.
    const char* stem = "entity";
    const char* component = "";
    switch (kind) {
    case Gameplay::Group:
        // No component at all: the point of a group is that it *is* only a
        // transform, so it cooks to a bare node the runtime ignores.
        stem = "group";
        break;
    case Gameplay::PlayerSpawn:
        stem = "player_spawn";
        component = "player_spawn";
        break;
    case Gameplay::Exit:
        stem = "exit";
        component = "exit";
        break;
    case Gameplay::Marker:
        stem = "marker";
        component = "marker";
        break;
    case Gameplay::EnemySpawn:
        stem = "enemy";
        component = "enemy_spawn";
        break;
    case Gameplay::Pickup:
        stem = "pickup";
        component = "pickup";
        break;
    case Gameplay::Trigger:
        stem = "trigger";
        component = "trigger";
        break;
    case Gameplay::PointLight:
        stem = "light";
        component = "light";
        break;
    case Gameplay::DirectionalLight:
        stem = "key_light";
        component = "light";
        break;
    }
    if (const ComponentType* type = findComponentType(component))
        type->add(entity, componentDefaults());

    if (kind == Gameplay::PointLight)
        entity.transform.position.y += 3.0f;
    if (kind == Gameplay::DirectionalLight) {
        // A key light is aimed, not placed: it is the rotation that matters,
        // and the height only keeps the gizmo out of the floor.
        entity.transform.position.y += 8.0f;
        entity.transform.rotationDegrees = {-55.0f, 30.0f, 0.0f};
        entity.light = LightAuthor{
            LightAuthor::Type::Directional, {0.95f, 0.93f, 0.88f}, 0.0f, true};
    }
    entity.id = mState.document.allocateId(stem);
    entity.name = entity.id;

    runCommand(makeCreateEntity(entity));
    selectAndReveal(entity.id, false);
    mPreview->invalidate();
}

void EditorApp::drawCatalog()
{
    focusPanelIfRequested("catalog");
    if (ImGui::Begin("Catalog", nullptr, kPanelFlags)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "filter", mCatalogFilter,
                                 sizeof(mCatalogFilter));
        ImGui::TextDisabled("brush: %s", mState.brushPrefab.empty()
                                             ? "(none)"
                                             : mState.brushPrefab.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("| rot %d deg", mBrushYawQuarters * 90);
        ImGui::Separator();

        // Gameplay entities are authored here too: they are part of the level's
        // vocabulary even though they have no mesh.
        if (ImGui::CollapsingHeader("gameplay")) {
            const auto button = [&](const char* label, Gameplay kind) {
                if (ImGui::Button(label, ImVec2(-1.0f, 0.0f)))
                    addGameplayEntity(kind);
            };
            button("group (empty node)", Gameplay::Group);
            button("player spawn", Gameplay::PlayerSpawn);
            // The level's portal IS its exit: the runtime builds the membrane,
            // surround and wisps around this entity. There is no portal mesh to
            // place, and naming the button after the thing an author sees in
            // game is what stops them looking for one.
            button("exit / portal", Gameplay::Exit);
            button("marker", Gameplay::Marker);
            button("enemy spawn", Gameplay::EnemySpawn);
            button("pickup", Gameplay::Pickup);
            button("trigger volume", Gameplay::Trigger);
            button("point light", Gameplay::PointLight);
            button("directional light", Gameplay::DirectionalLight);
            ImGui::Spacing();
            // Through the component registry, like every other add: the same
            // defaults, the whole selection, one undo entry. This used to be a
            // second implementation that only touched the primary and invented
            // its own half-extents.
            if (ImGui::Button("add collider to selection",
                              ImVec2(-1.0f, 0.0f))) {
                if (const ComponentType* collider =
                        findComponentType("collider"))
                    addComponentToSelection(*collider);
            }
        }

        if (ImGui::BeginChild("##pieces")) {
            const std::string filter = mCatalogFilter;
            // Grouped by role, which is how kit.toml is authored and how an
            // author thinks: "I need a wall", not "I need piece 17".
            for (const std::string& role : mState.catalog.roles()) {
                std::vector<const KitPiece*> pieces =
                    mState.catalog.byRole(role);
                std::vector<const KitPiece*> shown;
                for (const KitPiece* piece : pieces) {
                    if (filter.empty() ||
                        piece->id.find(filter) != std::string::npos ||
                        role.find(filter) != std::string::npos)
                        shown.push_back(piece);
                }
                if (shown.empty())
                    continue;
                if (!ImGui::CollapsingHeader(
                        role.c_str(),
                        filter.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
                    continue;
                for (const KitPiece* piece : shown) {
                    const bool active = mState.brushPrefab == piece->id;
                    // Strip the "kit." for display; the id still carries it.
                    const char* label = piece->id.c_str() + 4;
                    if (ImGui::Selectable(label, active)) {
                        mState.brushPrefab = piece->id;
                        mState.tool = Tool::Place;
                    }
                    // Per-piece detail is inherently dynamic, so it goes
                    // through the ad-hoc path with the shared styling rather
                    // than through the hint table.
                    char detail[512];
                    std::snprintf(detail, sizeof(detail),
                                  "socket %s  span %d\n%s",
                                  socketName(piece->socket), piece->span,
                                  piece->meshPath.c_str());
                    eng::imguihint::showText(piece->id.c_str(), detail);
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// The level's own properties: what it is called, what it costs, and the look it
// is lit and graded with.
//
// Shown in the Inspector when nothing is selected, which is where every engine
// puts world settings and which was two lines of dead space here.
void EditorApp::drawSceneProperties()
{
    ImGui::SeparatorText("scene");
    ImGui::TextDisabled("id      %s", mState.document.id.c_str());
    ImGui::TextDisabled("%zu entities", mState.document.entities.size());

    ImGui::Spacing();
    ImGui::SeparatorText("look");
    // The palette is the level's, not the session's: a crypt and a cathedral
    // are not the same room with different props, and until now the format
    // could not say which one this was.
    const std::string current =
        mState.document.palette.empty()
            ? std::string("(the game's default)")
            : mState.document.palette;
    if (ImGui::BeginCombo("palette", current.c_str())) {
        if (ImGui::Selectable("(the game's default)",
                              mState.document.palette.empty()))
            setScenePalette({});
        for (const std::string& name : mPalettes) {
            if (ImGui::Selectable(name.c_str(), name == mState.document.palette))
                setScenePalette(name);
        }
        ImGui::EndCombo();
    }
    if (!mState.document.palette.empty() &&
        std::find(mPalettes.begin(), mPalettes.end(), mState.document.palette) ==
            mPalettes.end()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "'%s' is not in palettes.toml",
                           mState.document.palette.c_str());
    }
    ImGui::TextDisabled("turn on the viewport's light switch to see it");
}

// Changing the palette is a document edit like any other, so it undoes.
void EditorApp::setScenePalette(const std::string& palette)
{
    if (mState.document.palette == palette)
        return;
    const std::string before = mState.document.palette;
    runCommand(Command{
        palette.empty() ? "clear palette" : "palette " + palette,
        [palette](Doc& doc) { doc.palette = palette; },
        [before](Doc& doc) { doc.palette = before; }});
    if (mEngine)
        applySceneEnvironment(mEngine->renderer());
}

void EditorApp::drawInspector()
{
    focusPanelIfRequested("inspector");
    if (!ImGui::Begin("Inspector", nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }
    const AuthorId* primary = mState.primary();
    Entity* entity = primary ? mState.document.find(*primary) : nullptr;
    if (!entity) {
        // With nothing selected the panel shows the level itself. It used to
        // show two lines of nothing, and the level's own properties -- the
        // palette it is lit and graded with -- had no home at all.
        drawSceneProperties();
        ImGui::End();
        return;
    }
    if (mState.selection.size() > 1) {
        ImGui::TextDisabled("%zu selected -- editing '%s'",
                            mState.selection.size(), entity->id.c_str());
        ImGui::TextDisabled("adding or removing a component hits all of them");
        ImGui::Separator();
    }

    // Fields are mutated live so the viewport follows the drag, and the command
    // is recorded when the widget is released -- one undo entry per edit, not
    // one per frame.
    const Entity before = *entity;
    InspectorContext context;
    context.catalog = &mState.catalog;
    context.materialNames = &mMaterialNames;
    context.enemyIds = &mEnemyIds;
    context.pickupIds = &mPickupIds;
    context.materials = &materialCatalog();
    context.meshKind = selectionMeshKind();

    drawEntityIdentity(*entity, context);

    // One collapsing section per component the entity carries, straight off the
    // registry. The panel has no idea what a light or a trigger is: adding a
    // component type means one entry in EntityComponents.cpp and one drawer in
    // ComponentInspector.cpp, and this loop picks it up.
    const ComponentType* removeRequested = nullptr;
    for (const ComponentType* type : componentsOf(*entity)) {
        ImGui::PushID(type->id);
        ImGui::SeparatorText(type->label);
        if (type->remove) {
            // Right-aligned so the sections read as a column of headers rather
            // than a column of buttons.
            ImGui::SameLine(ImGui::GetContentRegionAvail().x -
                            ImGui::GetFrameHeight());
            if (ImGui::SmallButton("x"))
                removeRequested = type;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("remove %s", type->label);
        }
        drawComponentBody(*type, *entity, context);
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
        ImGui::OpenPopup("##addcomponent");
    if (ImGui::BeginPopup("##addcomponent")) {
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##addfilter", "filter", mAddComponentFilter,
                                 sizeof(mAddComponentFilter));
        ImGui::Separator();
        const ComponentDefaults defaults = componentDefaults();
        const std::string filter = mAddComponentFilter;
        int offered = 0;
        for (const ComponentType* type : missingComponents(*entity)) {
            if (!filter.empty() &&
                std::string(type->label).find(filter) == std::string::npos &&
                std::string(type->id).find(filter) == std::string::npos)
                continue;
            ++offered;
            const bool ready = !type->addable || type->addable(defaults);
            if (ImGui::MenuItem(type->label, nullptr, false, ready)) {
                addComponentToSelection(*type);
                mAddComponentFilter[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s", ready ? type->hint : "pick a piece in the Catalog first");
        }
        if (offered == 0)
            ImGui::TextDisabled("nothing left to add");
        ImGui::EndPopup();
    }

    if (context.closed) {
        // The command captures the whole entity before and after -- small
        // enough to copy, and it means one code path per widget instead of one
        // command type per field.
        runCommand(
            makeEditEntity("edit " + entity->id, entity->id, before, *entity));
    }
    if (context.edited)
        mState.document.touch();
    // Deferred to here: removing a component invalidates `entity` through the
    // command stack, and the section loop above is still holding it.
    if (removeRequested)
        removeComponentFromSelection(*removeRequested);
    ImGui::End();
}

void EditorApp::drawIssues()
{
    focusPanelIfRequested("issues");
    if (ImGui::Begin("Issues", nullptr, kPanelFlags)) {
        // Revalidated when the document changes rather than every frame: it
        // walks every entity and the panel is often open while dragging.
        if (mIssuesRevision != mState.document.revision) {
            mIssuesRevision = mState.document.revision;
            mIssues =
                validate(mState.document, mState.catalog, mState.assetRoot);
        }

        int errors = 0;
        for (const Issue& issue : mIssues)
            errors += issue.severity == Severity::Error ? 1 : 0;
        ImGui::Text("%d issues (%d blocking)", int(mIssues.size()), errors);
        ImGui::SameLine();
        ImGui::TextDisabled("| cook: %s", mCookStatus.c_str());
        ImGui::Separator();

        if (ImGui::BeginChild("##issues")) {
            for (std::size_t i = 0; i < mIssues.size(); ++i) {
                const Issue& issue = mIssues[i];
                const ImVec4 colour = issue.severity == Severity::Error
                                          ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                                          : ImVec4(0.95f, 0.82f, 0.38f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, colour);
                const std::string label =
                    issue.code + "##issue" + std::to_string(i);
                if (ImGui::Selectable(label.c_str()) && !issue.entity.empty()) {
                    selectAndReveal(issue.entity, false);
                    frameSelectionOrAll();
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", issue.message.c_str());
                if (issue.fix != QuickFix::None) {
                    ImGui::SameLine();
                    const std::string fixLabel = "fix##" + std::to_string(i);
                    if (ImGui::SmallButton(fixLabel.c_str())) {
                        // Quick fixes go through the command stack like any
                        // other edit, so a fix that was wrong is one Ctrl+Z
                        // away.
                        const Entity* target =
                            issue.entity.empty()
                                ? nullptr
                                : mState.document.find(issue.entity);
                        Doc after = mState.document;
                        if (applyQuickFix(after, mState.catalog, issue)) {
                            if (target) {
                                const Entity* fixed = after.find(issue.entity);
                                if (fixed)
                                    runCommand(makeEditEntity(
                                        "fix " + issue.code, issue.entity,
                                        *target, *fixed));
                                else
                                    runCommand(makeDeleteEntity(mState.document,
                                                                issue.entity));
                            }
                            else {
                                // A document-level fix (adding a spawn): take
                                // whatever entity it introduced.
                                for (const Entity& entity : after.entities)
                                    if (!mState.document.contains(entity.id))
                                        runCommand(makeCreateEntity(entity));
                            }
                            mPreview->invalidate();
                        }
                    }
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// --- material staging mode ---------------------------------------------------

void EditorApp::setMode(bool material)
{
    if (mMaterialMode == material)
        return;
    mMaterialMode = material;
    // The staging mode's own panel comes forward with it; it shares a tab with
    // Particles and was landing behind it.
    if (material) mFocusMaterialFrames = 4;
    eng::Renderer& renderer = mEngine->renderer();

    if (material) {
        if (!mStage.built()) {
            mStage.build(renderer);
            mMaterialNames = renderer.materialNames();
            std::sort(mMaterialNames.begin(), mMaterialNames.end());
            mStage.setMaterial(renderer, mStage.material());
        }
        // A reference stage with nothing on it is not a reference. Entering the
        // mode with no material picked showed a bare floor, which reads as the
        // mode being broken rather than as "choose something".
        //
        // Chosen after the stage is built, because that is when the renderer's
        // material list exists -- and the catalogue is only trustworthy once it
        // has been crossed with that list.
        if (mSelectedMaterial.empty()) {
            // The game's own kit first. Alphabetical order put a demo scene's
            // pink crystal on the stage, which reads as the preview being
            // broken rather than as a material nobody asked for.
            for (const MaterialInfo& info : materialCatalog()) {
                if (info.name.rfind("Game/Kit/", 0) == 0) {
                    mSelectedMaterial = info.name;
                    break;
                }
            }
            for (const MaterialInfo& info : materialCatalog()) {
                if (!mSelectedMaterial.empty())
                    break;
                if (info.klass == MaterialClass::Surface ||
                    info.klass == MaterialClass::Atlas)
                    mSelectedMaterial = info.name;
            }
        }
        if (!mSelectedMaterial.empty())
            mStage.setMaterial(renderer, mSelectedMaterial);
        mStage.setVisible(renderer, true);
        mStage.applyEnvironment(renderer);
        // The level is hidden rather than unloaded: switching modes must not
        // cost a rebuild, and must never touch the document.
        mPreview->setVisible(renderer, false);
        mCameraBeforeMode = mState.camera;
        mState.camera.leaveWalk();
        mState.camera.setFlyPosition(mStage.cameraPosition());
        mState.camera.setYawPitch(mStage.cameraYaw(), mStage.cameraPitch());
        mState.camera.frame(mStage.focusPoint(), 4.0f);
    }
    else {
        mStage.setVisible(renderer, false);
        mPreview->setVisible(renderer, true);
        applySceneEnvironment(renderer);
        mState.camera = mCameraBeforeMode;
    }
}

void EditorApp::drawMaterialPanel()
{
    // Material and Particles share a dock slot as tabs, and entering the
    // staging mode has to bring this one forward or the mode switches with its
    // own panel hidden.
    //
    // Asked for *before* Begin, and for a few frames. Before, because Begin
    // returns false for a window whose tab is not selected -- so a focus call
    // after it never runs, which is exactly the bug this had. For a few frames,
    // because a layout restored from imgui.ini re-selects its saved tab on the
    // frame it is applied, which is after the first request.
    if (mFocusMaterialFrames > 0) {
        --mFocusMaterialFrames;
        ImGui::SetNextWindowFocus();
    }
    if (!ImGui::Begin("Material", nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }
    eng::Renderer& renderer = mEngine->renderer();

    // The swatch exists in both modes. Picking a material for a wall is the
    // common case, and it should not require leaving the level to see what the
    // material looks like.
    if (!mStage.thumbnailBuilt()) {
        mStage.buildThumbnail(renderer, 256);
        mMaterialNames = renderer.materialNames();
        std::sort(mMaterialNames.begin(), mMaterialNames.end());
    }

    // --- preview -----------------------------------------------------------
    const uint64_t thumbnail = renderer.materialThumbnailTextureId();
    const float thumbSize = 128.0f;
    if (thumbnail != 0) {
        ImGui::Image(static_cast<ImTextureID>(thumbnail),
                     ImVec2(thumbSize, thumbSize));
        // Drag on the swatch to turn the sphere: the cheapest way to check how
        // a material behaves at a different angle without changing anything.
        if (ImGui::IsItemActive() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            mThumbAutoSpin = false;
            mStage.spinThumbnail(renderer,
                                 mStage.thumbnailSpin() +
                                     ImGui::GetIO().MouseDelta.x * 0.01f);
        }
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::TextUnformatted(mSelectedMaterial.empty()
                               ? "(no material)"
                               : mSelectedMaterial.c_str());
    ImGui::TextDisabled("%zu materials", mMaterialNames.size());
    ImGui::Checkbox("spin", &mThumbAutoSpin);
    // Applying to the selection is the reason the panel exists in scene mode.
    const AuthorId* primary = mState.primary();
    const bool canApply = primary != nullptr && !mSelectedMaterial.empty();
    ImGui::BeginDisabled(!canApply);
    if (ImGui::Button("apply to selection"))
        applyMaterialToSelection(mSelectedMaterial);
    ImGui::EndDisabled();
    if (mMaterialMode) {
        if (ImGui::Button("show on stage"))
            mStage.setMaterial(renderer, mSelectedMaterial);
    }
    ImGui::EndGroup();

    ImGui::Separator();
    bool material = mMaterialMode;
    if (ImGui::Checkbox("staging mode", &material))
        setMode(material);
    ImGui::SameLine();
    eng::imguihint::marker(
        "editor.staging_mode",
        "Shows one material on a sphere over a reference floor, lit by the "
        "game's own shaders -- not a PBR preview, because the game does not "
        "render PBR. What you see here is what the dungeon will show.");

    if (mMaterialMode) {
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("turntable", &mStageSpinSpeed, -2.0f, 2.0f);
        ImGui::SameLine();
        ImGui::Checkbox("##autospin", &mStageAutoSpin);
        if (ImGui::RadioButton("grey", mFloorVariant == 0)) {
            mFloorVariant = 0;
            mStage.setFloorMaterial(renderer, "Editor/Checkerboard");
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("dark", mFloorVariant == 1)) {
            mFloorVariant = 1;
            mStage.setFloorMaterial(renderer, "Editor/CheckerboardDark");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("re-frame")) {
            mState.camera.setFlyPosition(mStage.cameraPosition());
            mState.camera.setYawPitch(mStage.cameraYaw(), mStage.cameraPitch());
        }
    }

    // --- the list ----------------------------------------------------------
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##materialfilter", "filter", mMaterialFilter,
                             sizeof(mMaterialFilter));
    // Most of the shipped catalogue cannot go on an entity at all: compositor
    // passes, particle materials, sprite and decal materials. Listing them
    // beside the ones an author is choosing between is how a bloom pass ends
    // up on a wall.
    ImGui::Checkbox("show non-surface materials", &mShowAllMaterials);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("particle, sprite and compositor materials -- they "
                          "need geometry the engine generates, not an entity's");

    // What the selection would take, so the list can say what will happen
    // before the click rather than after the cook.
    const MeshKind targetMesh = selectionMeshKind();
    if (ImGui::BeginChild("##materials")) {
        const std::string filter = mMaterialFilter;
        MaterialClass section = MaterialClass::Unknown;
        bool first = true;
        for (const MaterialInfo& info : materialCatalog()) {
            const std::string& name = info.name;
            if (!filter.empty() && name.find(filter) == std::string::npos)
                continue;
            if (!mShowAllMaterials && !isEntityMaterial(info.klass))
                continue;

            // Grouped by what a material *is*. The names alone do not say it:
            // "Engine/Psx/Fire" is a particle material and "Game/Kit/Stone" is
            // a wall, and they sort next to each other.
            if (first || info.klass != section) {
                section = info.klass;
                first = false;
                ImGui::SeparatorText(materialClassName(info.klass));
            }

            const MaterialAdvice advice = materialFits(info.klass, targetMesh);
            const bool warn = advice.fit != Fit::Good;
            if (warn) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      advice.fit == Fit::Broken
                                          ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                                          : ImVec4(0.95f, 0.82f, 0.38f, 1.0f));
            }
            const bool picked =
                ImGui::Selectable(name.c_str(), name == mSelectedMaterial);
            if (warn)
                ImGui::PopStyleColor();

            if (picked) {
                mSelectedMaterial = name;
                mStage.setThumbnailMaterial(renderer, name);
                if (mMaterialMode)
                    mStage.setMaterial(renderer, name);
            }
            // Hovering previews. Selecting commits. That split is what makes
            // scrubbing a long list to find the right material actually work.
            if (ImGui::IsItemHovered()) {
                mStage.setThumbnailMaterial(renderer, name);
                if (warn && !advice.reason.empty()) {
                    ImGui::SetTooltip("%s\n\n%s",
                                      advice.fit == Fit::Broken
                                          ? "Will not render on this selection:"
                                          : "Will render, but probably wrong:",
                                      advice.reason.c_str());
                } else if (!info.texture.empty()) {
                    ImGui::SetTooltip("%s  |  %s", info.texture.c_str(),
                                      info.vertexProgram.c_str());
                }
            }
            // Double-click applies straight to the selected entity: the fast
            // path once the author knows which material they want.
            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                mSelectedMaterial = name;
                applyMaterialToSelection(name);
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// Everything the shipped .material scripts declare, classified. Loaded once:
// the panel draws every frame and the catalogue is a few hundred lines of
// parsing.
const std::vector<MaterialInfo>& EditorApp::materialCatalog()
{
    if (mMaterialCatalog.empty() && !mMaterialCatalogLoaded) {
        mMaterialCatalogLoaded = true;
        const std::filesystem::path dir = eng::assets::resolve("materials");
        if (!dir.empty())
            mMaterialCatalog = loadMaterialCatalog(dir.string());

        // Crossed with what the renderer actually holds. The scripts on disk
        // are a superset: a material whose file is present but whose pack the
        // editor did not mount resolves to the missing-material pink, and
        // offering it is offering a broken result. The scripts say what a
        // material *is*; the renderer says whether it is here.
        if (!mMaterialNames.empty()) {
            std::vector<std::string> live = mMaterialNames;
            std::sort(live.begin(), live.end());
            const auto missing = [&live](const MaterialInfo& info) {
                return !std::binary_search(live.begin(), live.end(), info.name);
            };
            const std::size_t before = mMaterialCatalog.size();
            mMaterialCatalog.erase(std::remove_if(mMaterialCatalog.begin(),
                                                  mMaterialCatalog.end(),
                                                  missing),
                                   mMaterialCatalog.end());
            if (before != mMaterialCatalog.size()) {
                eng::log::info("Editor: %zu of %zu materials are not loaded",
                               before - mMaterialCatalog.size(), before);
            }
        }
        eng::log::info("Editor: %zu materials classified",
                       mMaterialCatalog.size());
    }
    return mMaterialCatalog;
}

const MaterialInfo* EditorApp::materialInfo(const std::string& name)
{
    for (const MaterialInfo& info : materialCatalog())
        if (info.name == name)
            return &info;
    return nullptr;
}

// What the selection's meshes offer a material.
//
// A kit piece declares the material it was authored with, and that declaration
// is what says whether its UVs index an atlas or wrap: no amount of looking at
// the .obj answers it. A mixed selection resolves to whatever they agree on,
// and Unknown when they do not -- which correctly suppresses advice rather than
// giving advice for one half of it.
MeshKind EditorApp::selectionMeshKind()
{
    MeshKind kind = MeshKind::Unknown;
    bool any = false;
    for (const AuthorId& id : mState.selection) {
        const Entity* entity = mState.document.find(id);
        if (!entity || entity->prefab.empty())
            continue;
        const KitPiece* piece = mState.catalog.find(entity->prefab);
        if (!piece)
            continue;
        const MaterialInfo* info = materialInfo(piece->material);
        const MeshKind one =
            info ? meshKindForMaterial(info->klass) : MeshKind::Unknown;
        if (!any) {
            kind = one;
            any = true;
        } else if (kind != one) {
            return MeshKind::Unknown;
        }
    }
    return kind;
}

// Live tuning for particle effects.
//
// Everything here edits eng::ParticleLibrary's descs in place and re-registers
// the changed effect, which is why a tweak shows up in the viewport on the next
// frame instead of at the next launch. Gameplay never sees any of this: the
// panel exists only in the editor, and the runtime it drives is the same one
// the game uses.
void EditorApp::drawParticlePanel()
{
    if (!ImGui::Begin("Particles", nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }
    eng::Renderer& renderer = mEngine->renderer();
    std::vector<eng::ParticleEffectDesc>& descs = mParticles.descs();

    if (descs.empty()) {
        ImGui::TextDisabled("no particles.toml loaded");
        ImGui::End();
        return;
    }

    // --- list --------------------------------------------------------------
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##fxfilter", "filter effects", mParticleFilter,
                             sizeof(mParticleFilter));
    const std::string filter = mParticleFilter;

    ImGui::BeginChild("##fxlist", ImVec2(0.0f, 160.0f), true);
    for (int i = 0; i < int(descs.size()); ++i) {
        if (!filter.empty() &&
            descs[size_t(i)].name.find(filter) == std::string::npos)
            continue;
        if (ImGui::Selectable(descs[size_t(i)].name.c_str(),
                              mParticleSelected == i))
            mParticleSelected = i;
    }
    ImGui::EndChild();

    ImGui::TextDisabled("%zu effects | %s", descs.size(),
                        mParticles.path().c_str());

    if (mParticleSelected < 0 || mParticleSelected >= int(descs.size())) {
        ImGui::End();
        return;
    }

    eng::ParticleEffectDesc& d = descs[size_t(mParticleSelected)];
    bool dirty = false;

    ImGui::Separator();

    // --- actions -----------------------------------------------------------
    // Spawning at the camera focus rather than at the origin: an effect is
    // judged where you are looking, and flying back to the origin to see each
    // tweak is what makes tuning tedious.
    const glm::vec3 spawnAt = mState.camera.target();
    if (ImGui::Button("Spawn")) {
        const eng::ParticlesHandle h = renderer.spawnParticles(d.name, spawnAt);
        if (h.valid())
            mParticlePreviews.push_back(h);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop all")) {
        for (eng::ParticlesHandle h : mParticlePreviews)
            renderer.despawnParticles(h);
        mParticlePreviews.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save TOML")) {
        if (mParticles.save(mParticles.path()))
            mStatus = "saved " + mParticles.path();
        else
            mStatus = "could not write " + mParticles.path();
    }
    ImGui::TextDisabled("%zu live preview(s)", mParticlePreviews.size());

    ImGui::Separator();

    // --- presentation ------------------------------------------------------
    dirty |= ImGui::DragFloat("width", &d.baseWidth, 0.005f, 0.001f, 4.0f);
    dirty |= ImGui::DragFloat("height", &d.baseHeight, 0.005f, 0.001f, 4.0f);
    dirty |= ImGui::DragInt("quota", &d.quota, 1.0f, 1, 4096);

    int mode = int(d.renderMode);
    if (ImGui::Combo("render mode", &mode, "sprite\0voxel\0")) {
        d.renderMode = eng::ParticleRenderMode(mode);
        dirty = true;
    }
    int role = int(d.visualRole);
    if (ImGui::Combo("visual role", &role,
                     "critical\0gameplay\0feedback\0ambient\0")) {
        d.visualRole = eng::ParticleVisualRole(role);
        dirty = true;
    }
    dirty |= ImGui::SliderFloat("quality weight", &d.qualityWeight, 0.0f, 1.0f);

    // --- motion ------------------------------------------------------------
    dirty |= ImGui::DragFloat3("acceleration", &d.acceleration.x, 0.05f);
    dirty |= ImGui::DragFloat("drag", &d.drag, 0.01f, 0.0f, 20.0f);
    dirty |= ImGui::Checkbox("loop", &d.loop);
    if (!d.loop)
        dirty |=
            ImGui::DragFloat("burst count", &d.burstCount, 1.0f, 0.0f, 4096.0f);
    dirty |= ImGui::Checkbox("local space", &d.localSpace);

    // --- variety -----------------------------------------------------------
    dirty |= ImGui::DragFloat("rotation jitter", &d.rotationJitterDeg, 1.0f,
                              0.0f, 360.0f);
    dirty |= ImGui::SliderFloat("hue jitter", &d.hueJitter, 0.0f, 1.0f);
    dirty |= ImGui::SliderFloat("scale jitter", &d.scaleJitter, 0.0f, 0.95f);

    // --- collision ---------------------------------------------------------
    int collide = int(d.collideResponse);
    if (ImGui::Combo("collide", &collide, "none\0die\0bounce\0decal\0")) {
        d.collideResponse = eng::ParticleCollideResponse(collide);
        dirty = true;
    }
    if (d.collideResponse == eng::ParticleCollideResponse::Bounce) {
        dirty |= ImGui::SliderFloat("restitution", &d.restitution, 0.0f, 1.0f);
        dirty |= ImGui::SliderFloat("friction", &d.friction, 0.0f, 1.0f);
    }
    if (d.collideResponse == eng::ParticleCollideResponse::Decal) {
        char profile[64] = {};
        std::snprintf(profile, sizeof(profile), "%s", d.decalProfile.c_str());
        if (ImGui::InputText("decal profile", profile, sizeof(profile))) {
            d.decalProfile = profile;
            dirty = true;
        }
    }

    // --- emitters ----------------------------------------------------------
    if (ImGui::CollapsingHeader("emitters")) {
        for (size_t e = 0; e < d.emitters.size(); ++e) {
            ImGui::PushID(int(e));
            eng::ParticleEmitterDesc& em = d.emitters[e];
            ImGui::SeparatorText(("emitter " + std::to_string(e)).c_str());
            int shape = int(em.shape);
            if (ImGui::Combo("shape", &shape, "point\0box\0")) {
                em.shape = eng::ParticleEmitterShape(shape);
                dirty = true;
            }
            if (em.shape == eng::ParticleEmitterShape::Box)
                dirty |= ImGui::DragFloat3("box size", &em.boxSize.x, 0.02f,
                                           0.001f, 32.0f);
            dirty |= ImGui::DragFloat3("position", &em.position.x, 0.02f);
            dirty |= ImGui::DragFloat3("direction", &em.direction.x, 0.02f);
            dirty |=
                ImGui::SliderFloat("angle", &em.angleDegrees, 0.0f, 180.0f);
            if (d.loop)
                dirty |= ImGui::DragFloat("rate", &em.emissionRate, 1.0f, 0.0f,
                                          4096.0f);
            dirty |= ImGui::DragFloatRange2("ttl", &em.ttlMin, &em.ttlMax,
                                            0.01f, 0.001f, 60.0f);
            dirty |=
                ImGui::DragFloatRange2("velocity", &em.velocityMin,
                                       &em.velocityMax, 0.02f, 0.0f, 200.0f);
            dirty |= ImGui::ColorEdit4("start colour", &em.startColour.x);
            ImGui::PopID();
        }
    }

    // --- ramps -------------------------------------------------------------
    // Colour and size are evaluated directly from these stops, so what the
    // panel shows is what the simulation runs -- there is no affector
    // approximation in between any more.
    if (ImGui::CollapsingHeader("colour ramp")) {
        for (size_t s = 0; s < d.colourRamp.size(); ++s) {
            ImGui::PushID(int(1000 + s));
            dirty |= ImGui::SliderFloat("t", &d.colourRamp[s].t, 0.0f, 1.0f);
            dirty |= ImGui::ColorEdit4("rgba", &d.colourRamp[s].rgba.x);
            if (ImGui::SmallButton("remove")) {
                d.colourRamp.erase(d.colourRamp.begin() + long(s));
                dirty = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("add colour stop")) {
            d.colourRamp.push_back({1.0f, glm::vec4(1.0f)});
            dirty = true;
        }
    }
    if (ImGui::CollapsingHeader("size ramp")) {
        for (size_t s = 0; s < d.sizeRamp.size(); ++s) {
            ImGui::PushID(int(2000 + s));
            dirty |= ImGui::SliderFloat("t", &d.sizeRamp[s].t, 0.0f, 1.0f);
            dirty |= ImGui::DragFloat("scale", &d.sizeRamp[s].scale, 0.01f,
                                      0.0f, 16.0f);
            if (ImGui::SmallButton("remove")) {
                d.sizeRamp.erase(d.sizeRamp.begin() + long(s));
                dirty = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("add size stop")) {
            d.sizeRamp.push_back({1.0f, 1.0f});
            dirty = true;
        }
    }

    // Re-registration keeps the effect id and the simulation slot, so live
    // instances survive the edit and simply pick up the new description.
    if (dirty)
        mParticles.reregister(renderer, size_t(mParticleSelected));

    ImGui::End();
}

// Sets a per-entity material override on everything selected. The kit piece's
// own material stays the default for every OTHER instance -- this is the
// one-off, not a way to restyle a whole level (kit.toml is that).
void EditorApp::applyMaterialToSelection(const std::string& material)
{
    if (material.empty() || mState.selection.empty())
        return;

    // Refused when it cannot render at all, and reported when it will render
    // wrongly. A material override is saved, cooked and shipped; finding out in
    // the game that a wall is wearing a compositor pass is finding out far too
    // late, and the symptom -- a black or missing surface -- points at the
    // renderer rather than at the click that caused it.
    if (const MaterialInfo* info = materialInfo(material)) {
        const MaterialAdvice advice =
            materialFits(info->klass, selectionMeshKind());
        if (advice.fit == Fit::Broken) {
            mStatus = material + ": " + advice.reason;
            eng::log::warn("Editor: refused %s -- %s", material.c_str(),
                           advice.reason.c_str());
            return;
        }
        if (advice.fit == Fit::Risky) {
            // Applied, and said. An author who wants a liquid on a kit piece is
            // allowed to want that; they should not be surprised by it.
            eng::log::info("Editor: %s on this selection -- %s",
                           material.c_str(), advice.reason.c_str());
        }
    }

    std::vector<Command> parts;
    for (const AuthorId& id : mState.selection) {
        const Entity* entity = mState.document.find(id);
        if (!entity || entity->prefab.empty() || entity->material == material)
            continue;
        Entity updated = *entity;
        updated.material = material;
        parts.push_back(makeEditEntity("set material", id, *entity, updated));
    }
    if (parts.empty()) {
        mStatus = "nothing in the selection takes a material override";
        return;
    }
    const std::size_t count = parts.size();
    runCommand(makeComposite("set material on " + std::to_string(count),
                             std::move(parts)));
    mPreview->invalidate();
    mStatus = "applied " + material + " to " + std::to_string(count) +
              (count == 1 ? " entity" : " entities");
    if (const MaterialInfo* info = materialInfo(material)) {
        const MaterialAdvice advice =
            materialFits(info->klass, selectionMeshKind());
        if (advice.fit == Fit::Risky)
            mStatus += "  --  " + advice.reason;
    }
}

// Save As. A plain path field rather than a file browser: the scenes all live
// in one directory, and a browser is a lot of UI for choosing between six
// files.
void EditorApp::drawOpenScenePopup()
{
    static constexpr const char* kTitle = "Open scene";
    if (mOpenSceneOpen && !ImGui::IsPopupOpen(kTitle))
        ImGui::OpenPopup(kTitle);
    ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoSavedSettings))
        return;

    const std::string directory = mState.assetRoot + "/scenes";
    ImGui::TextDisabled("%s", directory.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##openfilter", "filter", mOpenFilter,
                             sizeof(mOpenFilter));

    // The listing is read every frame the dialog is open. It is a directory of
    // a few dozen files and the dialog is open for seconds -- caching it would
    // mean showing a scene a colleague just deleted.
    const std::vector<SceneEntry> entries =
        filterScenes(listScenes(directory), mOpenFilter);

    if (ImGui::BeginChild("##scenes", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()))) {
        if (!mRecent.paths().empty() && mOpenFilter[0] == '\0') {
            ImGui::SeparatorText("recent");
            for (const std::string& path : mRecent.paths()) {
                const std::string name =
                    std::filesystem::path(path).filename().string();
                ImGui::PushID(path.c_str());
                if (ImGui::Selectable(name.c_str()))
                    requestOpen(path);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", path.c_str());
                ImGui::PopID();
            }
        }
        ImGui::SeparatorText("scenes");
        if (entries.empty()) {
            ImGui::TextDisabled(
                mOpenFilter[0] ? "nothing matches"
                               : "no scenes here yet -- Scene > New, then Save "
                                 "as...");
        }
        for (const SceneEntry& entry : entries) {
            ImGui::PushID(entry.path.c_str());
            const bool current = entry.path == mState.scenePath;
            if (ImGui::Selectable(entry.name.c_str(), current) && !current)
                requestOpen(entry.path);
            if (current) {
                ImGui::SameLine();
                ImGui::TextDisabled("(open)");
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        mOpenSceneOpen = false;
        ImGui::CloseCurrentPopup();
    }
    if (!mOpenSceneOpen)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Every binding, in one place, read from the same table the editor asks Input
// for. A shortcut nobody can find is a shortcut nobody uses, and this editor
// has thirty.
void EditorApp::drawSettings()
{
    if (!mSettingsOpen)
        return;
    ImGui::SetNextWindowSize(ImVec2(560.0f, 680.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin("Settings", &mSettingsOpen)) {
        ImGui::End();
        return;
    }

    // Written on every change, not on close: the setting most worth having is
    // the one that protects work from a crash, and it must survive the crash
    // that proves it was needed.
    bool changed = false;

    ImGui::SeparatorText("Autosave");
    ImGui::TextWrapped(
        "A backup of the open scene, written beside it as "
        "<name>.autosave.scn while there is unsaved work. Scene > Recover "
        "autosave reads it back.");
    ImGui::Spacing();

    changed |= ImGui::Checkbox("Back up automatically",
                               &mSettings.autosaveEnabled);

    ImGui::BeginDisabled(!mSettings.autosaveEnabled);
    // Minutes in the UI, seconds in the file: an author thinks "every two
    // minutes", and the clock counts seconds.
    float minutes = mSettings.autosaveSeconds / 60.0f;
    if (ImGui::SliderFloat("every", &minutes,
                           EditorSettings::kMinSeconds / 60.0f,
                           EditorSettings::kMaxSeconds / 60.0f, "%.1f min")) {
        mSettings.autosaveSeconds = minutes * 60.0f;
        changed = true;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("F5 saves the scene itself before it cooks, so a "
                       "playtest needs no backup of its own.");
    ImGui::PopStyleColor();
    ImGui::EndDisabled();

    ImGui::Spacing();
    // What the setting is doing right now. A backup schedule you cannot see is
    // a backup schedule you have to take on faith.
    if (!mSettings.autosaveEnabled) {
        ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.38f, 1.0f),
                           "off -- unsaved work is not being backed up");
    } else if (!mState.dirty) {
        ImGui::TextDisabled("nothing unsaved; the clock starts at the next edit");
    } else {
        ImGui::Text("next backup in %d:%02d", int(mAutosaveIn) / 60,
                    int(mAutosaveIn) % 60);
    }
    const std::string backup =
        autosavePath(mState.scenePath, mState.assetRoot + "/scenes");
    // Wrapped, not truncated: the point of showing the path is that somebody
    // can go and find the file.
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", backup.empty() ? "(no path yet)" : backup.c_str());
    ImGui::PopStyleColor();

    if (ImGui::Button("Back up now")) {
        writeAutosave();
        mAutosaveIn = mSettings.autosaveSeconds;
        mStatus = "wrote " + backup;
    }
    ImGui::SameLine();
    if (ImGui::Button("Recover autosave"))
        recoverAutosave();

    // --- the viewport ------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Viewport");
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("The same toggles as the View menu. They are here "
                       "because they are remembered: the profile you work "
                       "under should still be set tomorrow morning.");
    ImGui::PopStyleColor();

    // The render profile, by name. The preview of a per-entity ShaderParams --
    // a rim light, a tint -- is a function of this, which is why it is the
    // first row rather than a submenu three clicks away.
    const int currentPreset = mEngine ? mEngine->renderPreset() : 0;
    const char* currentName = eng::renderPresetName(currentPreset);
    if (ImGui::BeginCombo("look", currentName)) {
        for (const eng::RenderPresetInfo& preset : eng::renderPresets()) {
            if (ImGui::Selectable(preset.name, preset.id == currentPreset) &&
                mEngine) {
                mEngine->setRenderPreset(preset.id);
                mSettings.viewportPreset = preset.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Checkbox("Light the scene as the game will", &mGameLighting)) {
        mSettings.gameLighting = mGameLighting;
        if (mEngine)
            applySceneEnvironment(mEngine->renderer());
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("the level's own palette instead of the editor's "
                          "flat work light -- contrast is what tells you "
                          "whether a room reads");
    if (ImGui::Checkbox("Entity marks", &mShowEntityGizmos))
        changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Volumes", &mShowGizmoVolumes))
        changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Frame stats", &mShowFrameStats))
        changed = true;

    // --- playtest ----------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Playtest");
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("F5 cooks the scene and launches the game as a separate "
                       "process. These are the switches it starts with -- each "
                       "one is an option the game already has.");
    ImGui::PopStyleColor();

    changed |= ImGui::Checkbox("Play under the viewport's look",
                               &mSettings.playtestMatchesViewport);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("a rim light or a tint tuned in the editor reads "
                          "completely differently under another profile; "
                          "matching is what makes the tuning mean something");
    ImGui::BeginDisabled(mSettings.playtestMatchesViewport);
    {
        const std::string& chosen = mSettings.playtestPreset;
        // While it is linked, the greyed combo shows what it is linked *to*.
        // Showing the parked choice instead reads as "this is what will run",
        // which is exactly what it is not.
        const std::string matched = playtestPresetName();
        const std::string shown =
            mSettings.playtestMatchesViewport ? matched : chosen;
        const char* label = shown.empty() ? "(the game's default)" : shown.c_str();
        if (ImGui::BeginCombo("plays under", label)) {
            if (ImGui::Selectable("(the game's default)", chosen.empty())) {
                mSettings.playtestPreset.clear();
                changed = true;
            }
            for (const eng::RenderPresetInfo& preset : eng::renderPresets()) {
                if (ImGui::Selectable(preset.name, chosen == preset.name)) {
                    mSettings.playtestPreset = preset.name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::EndDisabled();

    if (ImGui::Checkbox("Start where the camera is", &mPlayFromCamera)) {
        mSettings.playFromCamera = mPlayFromCamera;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("off starts at the player spawn, which is how the "
                          "arrival itself gets checked");
    changed |= ImGui::Checkbox("Open the debug console",
                               &mSettings.playtestConsole);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Show colliders", &mSettings.playtestColliders);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Fullscreen", &mSettings.playtestFullscreen);

    // Exactly what F5 will run. A launcher whose switches are invisible is a
    // launcher you end up debugging by reading its source.
    {
        PlaytestEnvironment preview;
        preview.renderPreset = playtestPresetName();
        preview.console = mSettings.playtestConsole;
        preview.colliders = mSettings.playtestColliders;
        preview.fullscreen = mSettings.playtestFullscreen;
        if (mPlayFromCamera)
            preview.playFrom = "<camera>";
        std::string line = "game <scene>.map";
        for (const std::string& entry : playtestEnvironment(preview))
            line += "  " + entry;
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", line.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Where these live");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", mSettingsFile.c_str());
    ImGui::PopStyleColor();
    ImGui::TextWrapped(
        "Per-user, like the recent-files list. The scene format, the key "
        "bindings and the window size are project content and stay in "
        "config/editor.toml.");
    if (ImGui::Button("Restore defaults")) {
        mSettings = EditorSettings{};
        changed = true;
    }

    if (changed)
        commitSettings();
    ImGui::End();
}

void EditorApp::drawHelp()
{
    if (!mHelpOpen)
        return;
    ImGui::SetNextWindowSize(ImVec2(460.0f, 520.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin("Shortcuts", &mHelpOpen)) {
        ImGui::End();
        return;
    }

    struct Row {
        const char* keys;
        const char* what;
    };
    const auto section = [](const char* title, const Row* rows, int count) {
        ImGui::SeparatorText(title);
        if (!ImGui::BeginTable(title, 2, ImGuiTableFlags_SizingStretchProp))
            return;
        for (int i = 0; i < count; ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", rows[i].keys);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(rows[i].what);
        }
        ImGui::EndTable();
    };

    static const Row kNavigate[] = {
        {"hold RMB", "fly the camera"},
        {"WASD / Q E", "move while flying"},
        {"Shift", "fly faster"},
        {"F", "frame the selection, or the whole scene"},
        {"V", "walk the level from the player's eye"},
        {"middle drag", "orbit"},
    };
    static const Row kTools[] = {
        {"Q / W / E", "select / place / room tool"},
        {"R", "cycle gizmo: translate, rotate, scale"},
        {"X", "gizmo axes: world or local"},
        {"G", "snap to grid"},
        {"[ / ]", "coarser / finer grid subdivision"},
        {"PgUp / PgDn / Home", "raise, lower, reset the work plane"},
    };
    static const Row kEdit[] = {
        {"Ctrl+Z / Ctrl+Y", "undo / redo"},
        {"Ctrl+C / Ctrl+X", "copy / cut selection"},
        {"Ctrl+V", "paste, offset one cell"},
        {"Ctrl+D", "duplicate, offset one cell"},
        {"Delete", "delete selection"},
        {"Shift+click", "extend the selection"},
    };
    static const Row kScene[] = {
        {"Ctrl+P", "command palette -- every verb, by name"},
        {"Ctrl+O", "open a scene"},
        {"Ctrl+S", "save"},
        {"Ctrl+R", "reload from disk"},
        {"F6", "cook to a runtime map"},
        {"F5", "cook and playtest"},
        {"`", "developer console"},
        {"F1", "this panel"},
        {"Esc", "cancel, then deselect, then quit"},
    };
    section("navigate", kNavigate, IM_ARRAYSIZE(kNavigate));
    section("tools", kTools, IM_ARRAYSIZE(kTools));
    section("edit", kEdit, IM_ARRAYSIZE(kEdit));
    section("scene", kScene, IM_ARRAYSIZE(kScene));

    ImGui::Spacing();
    ImGui::TextWrapped(
        "Letter keys are mute while a text field has focus, and while the "
        "camera is flying -- W is both 'forward' and 'place tool'.");
    ImGui::End();
}

void EditorApp::drawSaveAsPopup()
{
    if (mSaveAsOpen) {
        ImGui::OpenPopup("Save scene as");
        mSaveAsOpen = false;
    }
    if (!ImGui::BeginPopupModal("Save scene as", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted("Path (.scn)");
    ImGui::SetNextItemWidth(520.0f);
    const bool entered =
        ImGui::InputText("##saveaspath", mSaveAsPath, sizeof(mSaveAsPath),
                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (entered || ImGui::Button("Save", ImVec2(120.0f, 0.0f))) {
        mState.scenePath = mSaveAsPath;
        if (saveScene())
            ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::drawStatusBar()
{
    if (ImGui::Begin("Status", nullptr, kPanelFlags)) {
        ImGui::TextUnformatted(mStatus.c_str());
        if (!mPreview->lastError().empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "preview: %s",
                               mPreview->lastError().c_str());
        }
        ImGui::Text(
            "%s%s | cook: %s | undo: %s",
            mState.scenePath.empty() ? "(no scene)"
                                     : std::filesystem::path(mState.scenePath)
                                           .filename()
                                           .string()
                                           .c_str(),
            mState.dirty ? " *" : "", mCookStatus.c_str(),
            mCommands.canUndo() ? mCommands.undoLabel().c_str() : "(empty)");

        // What is selected, by name. The batch and triangle counts moved to the
        // viewport corner, where they are watched; this line is about the
        // document, and "which of these forty pillars am I holding" was the
        // question it could not answer.
        const glm::vec3 eye = mState.camera.activeEye();
        if (mState.selection.empty()) {
            ImGui::Text("camera %.1f %.1f %.1f | nothing selected", double(eye.x),
                        double(eye.y), double(eye.z));
        } else {
            const Entity* primary = mState.document.find(*mState.primary());
            const std::string name =
                primary ? (primary->name.empty() ? primary->id : primary->name)
                        : std::string("(gone)");
            const std::string extra =
                mState.selection.size() > 1
                    ? "  +" + std::to_string(mState.selection.size() - 1)
                    : std::string();
            // Inside a composed object, say which one. Otherwise "candle_0003"
            // is the whole story the editor tells about a selection whose drag
            // will move something bigger, and the only way to find out is to
            // drag it.
            std::string within;
            if (primary && !primary->parent.empty()) {
                const AuthorId root = rootOf(mState.document, primary->id);
                const Entity* object = mState.document.find(root);
                within = "   (in " +
                         (object ? (object->name.empty() ? object->id
                                                         : object->name)
                                 : root) +
                         ")";
            }
            ImGui::Text("camera %.1f %.1f %.1f | %s%s%s", double(eye.x),
                        double(eye.y), double(eye.z), name.c_str(),
                        extra.c_str(), within.c_str());
        }
    }
    ImGui::End();
}

void EditorApp::onShutdown(eng::Engine&)
{
    mPreview.reset();
}

} // namespace ed
