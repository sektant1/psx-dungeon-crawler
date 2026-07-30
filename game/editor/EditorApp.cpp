#include "EditorApp.h"

#include "Picker.h"

#include <optional>
#include "SceneCook.h"

#include <algorithm>
#include "SceneSource.h"
#include "SceneTemplates.h"
#include "SceneValidate.h"
#include "SceneWriter.h"

#include <eng/Input.h>
#include <eng/Log.h>
#include <eng/Renderer.h>

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

// Grid drawn as world-space debug lines rather than an ImGui overlay: it has to
// sit *under* the geometry and take perspective, which a 2D draw list cannot do.
constexpr int kGridRadius = 16; // cells drawn either side of the camera

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoCollapse;

} // namespace

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

eng::AppConfig EditorApp::configure(int argc, char** argv)
{
    const std::string assets = APP_ASSET_DIR;
    mExecutablePath = argc > 0 ? argv[0] : "scene_editor";
    mState.assetRoot = assets;
    mState.kitPath = assets + "/kit.toml";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.size() > 4 && arg.substr(arg.size() - 4) == ".scn")
            mPendingScene = arg;
    }
    if (mPendingScene.empty())
        mPendingScene = assets + "/scenes/tech_demo.scn";

    eng::AppConfig config;
    config.assetDir = assets;
    config.configPath = assets + "/editor.toml";
    config.fixedDt = 0.0f; // nothing here is simulated
    config.imgui = true;
    return config;
}

bool EditorApp::onStart(eng::Engine& engine)
{
    mEngine = &engine;
    eng::Renderer& renderer = engine.renderer();

    std::string error;
    if (!KitCatalog::load(mState.kitPath, mState.catalog, error)) {
        eng::log::error("editor: %s", error.c_str());
        exitCode = 1;
        return false;
    }
    mState.grid = GridConfig::fromCatalog(mState.catalog);

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
    renderer.setOrientation(keyNode, glm::angleAxis(glm::radians(-55.0f),
                                                    glm::vec3(1.0f, 0.0f, 0.0f)));
    renderer.attachLight(keyNode, key);

    mPreview = std::make_unique<PreviewBridge>(renderer, mState.assetRoot);
    mState.camera.setFlyPosition({0.0f, 14.0f, 26.0f});
    mState.camera.setYawPitch(0.0f, -0.45f);

    if (!mPendingScene.empty())
        loadScene(mPendingScene);
    // Verification hook: start in the staging scene so a screenshot run can
    // capture it without driving the UI.
    if (std::getenv("PSX_EDITOR_MATERIAL"))
        setMode(true);
    // Verification hooks: drive the two interactions a screenshot run cannot
    // click on its own.
    if (std::getenv("PSX_EDITOR_CYCLE_MATERIALS"))
        mCycleMaterials = true;
    // Verification hook: build a room without a mouse, so a screenshot run can
    // show what the tool produces.
    if (const char* room = std::getenv("PSX_EDITOR_DEMO_ROOM")) {
        mState.document = SceneDocument{};
        mState.document.id = "scene.demo_room";
        int w = 4, d = 3;
        std::sscanf(room, "%dx%d", &w, &d);
        RoomSpec spec = mState.roomSpec;
        spec.col0 = 0; spec.row0 = 0;
        spec.col1 = w - 1; spec.row1 = d - 1;
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
        if (!mState.document.entities.empty()) {
            const std::size_t index =
                std::size_t(std::atoi(select)) % mState.document.entities.size();
            mState.select(mState.document.entities[index].id);
        }
    }

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
            piece->localBoundsMeters(mState.catalog.scale(), localMin, localMax);
        // Axis-aligned approximation: good enough to frame a camera, and it
        // does not need the mesh to be loaded.
        min = glm::min(min, entity.transform.position + localMin);
        max = glm::max(max, entity.transform.position + localMax);
        any = true;
    };
    if (ids.empty()) {
        for (const Entity& entity : mState.document.entities)
            include(entity);
    } else {
        for (const AuthorId& id : ids)
            if (const Entity* entity = mState.document.find(id)) include(*entity);
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
    const float distance = radius / std::tan(glm::radians(65.0f * 0.5f)) * 1.25f;
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

// A fresh document from a template. Templates always include a spawn and an
// exit, so a new scene cooks and plays from the first frame -- handing someone a
// document that refuses to run is a bad first thirty seconds.
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
    mStatus = "saved " +
              std::filesystem::path(mState.scenePath).filename().string();
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
    if (mState.dirty && !saveScene())
        return;
    std::string mapPath;
    if (!cookScene(mapPath))
        return;

    const std::string exe = siblingExecutable(mExecutablePath, "game");
    const std::string log = mState.assetRoot + "/../../playtest.log";
    mPlaytest = launchGame(exe, mapPath, log);
    if (!mPlaytest.running()) {
        mStatus = mPlaytest.error;
        return;
    }
    mStatus = "playtest running (pid " + std::to_string(mPlaytest.pid) + ")";
}

void EditorApp::deleteSelection()
{
    if (mState.selection.empty())
        return;
    std::vector<Command> parts;
    for (const AuthorId& id : mState.selection)
        parts.push_back(makeDeleteEntity(mState.document, id));
    runCommand(makeComposite("delete " + std::to_string(parts.size()) +
                                 " entities",
                             std::move(parts)));
    mState.selection.clear();
}

void EditorApp::duplicateSelection()
{
    if (mState.selection.empty())
        return;
    std::vector<Command> parts;
    std::vector<AuthorId> fresh;
    // Ids are allocated against a working copy so a batch duplicate cannot hand
    // out the same id twice.
    Doc probe = mState.document;
    for (const AuthorId& id : mState.selection) {
        const Entity* source = mState.document.find(id);
        if (!source)
            continue;
        Entity copy = *source;
        copy.id = probe.allocateId(source->prefab.empty() ? source->id
                                                          : source->prefab);
        probe.add(copy);
        fresh.push_back(copy.id);
        parts.push_back(makeCreateEntity(copy));
    }
    if (parts.empty())
        return;
    runCommand(makeComposite("duplicate", std::move(parts)));
    mState.selection = fresh;
}

void EditorApp::onFrameBegin(const eng::FrameContext& f)
{
    eng::Input& input = f.engine.input();

    // Right button held over the viewport = fly.
    //
    // Deliberately NOT gated on io.WantCaptureMouse: the viewport IS an ImGui
    // window, so ImGui always wants the mouse while the cursor is over it, and
    // testing that flag meant the fly camera could never engage anywhere it was
    // useful. mViewportHovered (ImGui::IsWindowHovered on the viewport panel) is
    // the question actually being asked -- is the cursor over the 3D view.
    const bool wantsFly = input.isMouseDown(eng::MouseButton::Right) &&
                          (mFlying || mViewportHovered);
    if (wantsFly != mFlying) {
        mFlying = wantsFly;
        input.setMouseGrab(mFlying);
    }

    if (mFlying) {
        const glm::vec2 delta = input.mouseDelta();
        constexpr float kLookSpeed = 0.0032f;
        mState.camera.addYawPitch(-delta.x * kLookSpeed, -delta.y * kLookSpeed);

        // WASD + QE, the movement every 3D editor since Quake has used.
        glm::vec3 move{0.0f};
        if (input.isDown("fly_forward")) move.z -= 1.0f;
        if (input.isDown("fly_back")) move.z += 1.0f;
        if (input.isDown("fly_left")) move.x -= 1.0f;
        if (input.isDown("fly_right")) move.x += 1.0f;
        if (input.isDown("fly_down")) move.y -= 1.0f;
        if (input.isDown("fly_up")) move.y += 1.0f;
        if (move != glm::vec3(0.0f)) {
            const float speed =
                (input.isDown("fly_fast") ? 36.0f : 12.0f) * f.dt;
            mState.camera.moveLocal(glm::normalize(move) * speed);
        }
    }

    // Letter shortcuts stay mute while a text field owns the keyboard, so
    // typing a name into the inspector cannot fly the camera or delete a thing.
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        if (input.wasPressed("focus"))
            frameSelectionOrAll();
        if (input.wasPressed("tool_select") && !mFlying)
            mState.tool = Tool::Select;
        if (input.wasPressed("tool_place") && !mFlying)
            mState.tool = Tool::Place;
        if (input.wasPressed("tool_room") && !mFlying)
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
        if (input.wasPressed("rotate"))
            mGizmoOperation = (mGizmoOperation + 1) % 3;
        if (input.wasPressed("delete"))
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
        if (io.KeyCtrl && input.wasPressed("undo")) {
            if (io.KeyShift ? mCommands.redo(mState.document)
                            : mCommands.undo(mState.document)) {
                mState.document.touch();
                mPreview->invalidate();
                mState.dirty = !mCommands.savedStateReached();
            }
        }
        if (io.KeyCtrl && input.wasPressed("redo")) {
            if (mCommands.redo(mState.document)) {
                mState.document.touch();
                mPreview->invalidate();
                mState.dirty = !mCommands.savedStateReached();
            }
        }
    }

    if (input.wasPressed("quit"))
        f.engine.requestClose();
}

void EditorApp::onUpdate(const eng::FrameContext& f)
{
    eng::Renderer& renderer = f.engine.renderer();
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
    } else {
        mPreview->sync(mState.document, mState.catalog);
    }
    renderer.setEditorCameraPose(mState.camera.flyEye(),
                                 mState.camera.flyOrientation(), 65.0f);
    if (mMaterialMode)
        renderer.setDebugLines({}); // the checkerboard is the reference here
    else
        updateGridLines(renderer);
    renderer.frameStats(mBatches, mTriangles);

    int exitCode = 0;
    if (mPlaytest.running() && !pollGame(mPlaytest, exitCode)) {
        mStatus = exitCode == 0
                      ? "playtest finished"
                      : "playtest exited with code " + std::to_string(exitCode) +
                            " -- see playtest.log";
    }
}

// Editor lighting for level editing: bright and flat, because the job there is
// to see where things are, not how they are lit. The staging scene deliberately
// replaces this with something much darker.
void EditorApp::applySceneEnvironment(eng::Renderer& renderer)
{
    renderer.setBackground({0.05f, 0.055f, 0.07f});
    renderer.setAmbient({0.45f, 0.46f, 0.5f});
    renderer.setFog({0.05f, 0.055f, 0.07f}, 0.0f);
    renderer.setEditorViewportBackground({0.10f, 0.11f, 0.13f});
}

void EditorApp::updateGridLines(eng::Renderer& renderer)
{
    static std::vector<eng::Renderer::DebugLine> lines;
    lines.clear();

    const float cell = mState.grid.cell;
    const float level = mState.gridState.level;
    // Centred on the camera so the grid never runs out from under the view.
    const glm::vec3 eye = mState.camera.flyEye();
    const int centreCol = int(std::floor(eye.x / cell));
    const int centreRow = int(std::floor(eye.z / cell));

    const glm::vec3 minor{0.22f, 0.24f, 0.30f};
    const glm::vec3 major{0.38f, 0.42f, 0.52f};
    const glm::vec3 axisX{0.55f, 0.25f, 0.28f};
    const glm::vec3 axisZ{0.25f, 0.40f, 0.60f};

    for (int i = -kGridRadius; i <= kGridRadius; ++i) {
        const int col = centreCol + i;
        const int row = centreRow + i;
        const float x = float(col) * cell;
        const float z = float(row) * cell;
        const float far = float(kGridRadius) * cell;
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

void EditorApp::onGui(const eng::FrameContext& f)
{
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
            const ImGuiID top =
                ImGui::DockBuilderSplitNode(centre, ImGuiDir_Up, 0.06f,
                                            nullptr, &centre);
            const ImGuiID bottom =
                ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.10f,
                                            nullptr, &centre);
            const ImGuiID right =
                ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.22f,
                                            nullptr, &centre);
            const ImGuiID left =
                ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.20f,
                                            nullptr, &centre);

            ImGui::DockBuilderDockWindow("Toolbar", top);
            ImGui::DockBuilderDockWindow("Status", bottom);
            // Inspector above, Material below it -- the arrangement every DCC
            // and engine editor converged on, because the preview is read while
            // editing the thing above it.
            ImGuiID rightTop = right;
            const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(
                rightTop, ImGuiDir_Down, 0.45f, nullptr, &rightTop);
            ImGui::DockBuilderDockWindow("Inspector", rightTop);
            ImGui::DockBuilderDockWindow("Material", rightBottom);
            ImGui::DockBuilderDockWindow("Outliner", left);
            ImGui::DockBuilderDockWindow("Catalog", left);
            ImGui::DockBuilderDockWindow("Issues", bottom);
            ImGui::DockBuilderDockWindow("Viewport", centre);
            ImGui::DockBuilderFinish(dock);
        }
    }
    ImGui::End();

    drawToolbar();
    drawViewport(f);
    drawOutliner();
    drawCatalog();
    drawInspector();
    drawIssues();
    drawMaterialPanel();
    drawStatusBar();
    drawSaveAsPopup();
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
                    newScene(which);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !mState.scenePath.empty()))
            saveScene();
        if (ImGui::MenuItem("Save as...")) {
            mSaveAsOpen = true;
            std::snprintf(mSaveAsPath, sizeof(mSaveAsPath), "%s",
                          mState.scenePath.empty()
                              ? (mState.assetRoot + "/scenes/untitled.scn").c_str()
                              : mState.scenePath.c_str());
        }
        if (ImGui::MenuItem("Reload", "Ctrl+R", false, !mState.scenePath.empty()))
            loadScene(mState.scenePath);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Esc"))
            f.engine.requestClose();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const std::string undo = "Undo " + mCommands.undoLabel();
        const std::string redo = "Redo " + mCommands.redoLabel();
        if (ImGui::MenuItem(undo.c_str(), "Ctrl+Z", false,
                            mCommands.canUndo())) {
            mCommands.undo(mState.document);
            mState.document.touch();
            mPreview->invalidate();
            mState.dirty = !mCommands.savedStateReached();
        }
        if (ImGui::MenuItem(redo.c_str(), "Ctrl+Shift+Z", false,
                            mCommands.canRedo())) {
            mCommands.redo(mState.document);
            mState.document.touch();
            mPreview->invalidate();
            mState.dirty = !mCommands.savedStateReached();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false,
                            !mState.selection.empty()))
            duplicateSelection();
        if (ImGui::MenuItem("Delete", "Del", false, !mState.selection.empty()))
            deleteSelection();
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
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Snap to grid", "G", &mState.gridState.snap);
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
        ImGui::SameLine();
        if (ImGui::SmallButton("[")) mState.gridState.coarser();
        ImGui::SameLine();
        if (ImGui::SmallButton("]")) mState.gridState.finer();
        ImGui::SameLine();
        ImGui::Checkbox("snap", &mState.gridState.snap);
        ImGui::SameLine();
        ImGui::Text("| level %.1f m", double(mState.gridState.level));
        ImGui::SameLine();
        if (ImGui::SmallButton("-")) mState.gridState.level -= mState.grid.cell;
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) mState.gridState.level += mState.grid.cell;

        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        if (ImGui::SmallButton("Frame (F)"))
            frameSelectionOrAll();

        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        if (ImGui::SmallButton(mPlaytest.running() ? "Stop" : "Run (F5)"))
            runPlaytest();
        ImGui::SameLine();
        if (ImGui::SmallButton("Cook (F6)")) {
            std::string mapPath;
            cookScene(mapPath);
        }
        if (mState.dirty) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.38f, 1.0f), "* unsaved");
        }
    }
    ImGui::End();
}

void EditorApp::drawViewport(const eng::FrameContext& f)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("Viewport", nullptr,
                     kPanelFlags | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        mViewportX = pos.x;
        mViewportY = pos.y;

        if (size.x > 8.0f && size.y > 8.0f) {
            if (int(size.x) != int(mViewportW) || int(size.y) != int(mViewportH)) {
                mViewportW = size.x;
                mViewportH = size.y;
                f.engine.renderer().resizeEditorViewport(int(size.x),
                                                         int(size.y));
            }
            const uint64_t texture = f.engine.renderer().editorViewportTextureId();
            if (texture != 0) {
                // Default uv: OGRE's render-to-texture already hands back a
                // top-down image, so flipping V here turned the whole world
                // upside down.
                ImGui::Image(static_cast<ImTextureID>(texture), size);
            } else {
                ImGui::TextUnformatted("offscreen viewport unavailable");
            }
        }
        mViewportHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        // Gizmo first, then picking: a click that lands on a gizmo handle is a
        // drag, not a selection change.
        if (mMaterialMode) {
            // Only the test shape may be manipulated: a reference stage whose
            // floor and lights can be dragged out of alignment has stopped
            // being a reference.
            drawStageGizmo(f);
        } else if (mState.tool == Tool::Room) {
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
        } else if (mState.tool == Tool::Place) {
            drawPlacementGhost(f);
            if (mViewportHovered && !mFlying) {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    mPainting = true;
                    mPaintedSlots.clear();
                    mPaintParts.clear();
                }
                if (mPainting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    CellPlacement cell;
                    XformAuthor transform;
                    if (hoveredPlacement(f, cell, transform))
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
        } else {
            drawGizmo(f);
            handleViewportPicking(f);
        }
    } else {
        mViewportHovered = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// Camera basis shared by the gizmo and the picker, so a click and the handle it
// lands on can never disagree about where the camera is.
static void cameraMatrices(const EditorCamera& camera, float aspect,
                           glm::mat4& view, glm::mat4& projection)
{
    const glm::vec3 eye = camera.flyEye();
    const glm::quat orientation = camera.flyOrientation();
    const glm::vec3 forward = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 up = orientation * glm::vec3(0.0f, 1.0f, 0.0f);
    view = glm::lookAt(eye, eye + forward, up);
    projection = glm::perspective(glm::radians(65.0f), aspect, 0.05f, 4000.0f);
}

void EditorApp::handleViewportPicking(const eng::FrameContext& f)
{
    if (!mViewportHovered || mFlying || ImGuizmo::IsOver() || ImGuizmo::IsUsing())
        return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return;

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const glm::vec2 ndc{(mouse.x - mViewportX) / mViewportW * 2.0f - 1.0f,
                        1.0f - (mouse.y - mViewportY) / mViewportH * 2.0f};
    const Ray ray = screenRay(ndc, mState.camera.flyEye(),
                              mState.camera.flyOrientation(),
                              glm::radians(65.0f), mViewportW / mViewportH);

    // Tested against catalogue bounds rather than the loaded meshes: an entity
    // has to be selectable even before its mesh is on disk, which is exactly
    // when the author most needs to click it and see why.
    const AuthorId* hit = nullptr;
    float bestT = 1e30f;
    float bestVolume = 1e30f;
    for (const Entity& entity : mState.document.entities) {
        glm::vec3 min(-0.5f), max(0.5f);
        if (const KitPiece* piece = mState.catalog.find(entity.prefab))
            piece->localBoundsMeters(mState.catalog.scale(), min, max);
        min += entity.transform.position;
        max += entity.transform.position;
        float t = 0.0f;
        if (!rayAabb(ray, min, max, t))
            continue;
        const glm::vec3 size = max - min;
        const float volume = size.x * size.y * size.z;
        // Ties go to the smaller box, so clicking a barrel inside a room does
        // not select the room.
        if (t < bestT - 1e-3f || (std::fabs(t - bestT) <= 1e-3f &&
                                  volume < bestVolume)) {
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
    if (ImGui::GetIO().KeyShift)
        mState.toggleSelected(*hit);
    else
        mState.select(*hit);
}

void EditorApp::drawGizmo(const eng::FrameContext& f)
{
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
    const glm::vec3 anchor = haveBounds ? (boundsMin + boundsMax) * 0.5f
                                        : primary->transform.position;

    glm::mat4 matrix(1.0f);
    const XformAuthor& xform = primary->transform;
    ImGuizmo::RecomposeMatrixFromComponents(
        glm::value_ptr(anchor), glm::value_ptr(xform.rotationDegrees),
        glm::value_ptr(xform.scale), glm::value_ptr(matrix));

    const ImGuizmo::OPERATION operation =
        mGizmoOperation == 1 ? ImGuizmo::ROTATE
                             : (mGizmoOperation == 2 ? ImGuizmo::SCALE
                                                     : ImGuizmo::TRANSLATE);
    // Snap applies to free objects; a kit piece with a cell is re-snapped to it
    // when the drag closes, so its own step is the one that matters.
    const float step = mState.gridState.step();
    const glm::vec3 snap{step, step, step};
    const glm::vec3 rotateSnap{15.0f, 15.0f, 15.0f};
    const float* snapPtr =
        mState.gridState.snap
            ? (operation == ImGuizmo::ROTATE ? glm::value_ptr(rotateSnap)
                                             : glm::value_ptr(snap))
            : nullptr;

    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                         operation, ImGuizmo::WORLD, glm::value_ptr(matrix),
                         nullptr, snapPtr);

    if (ImGuizmo::IsUsing()) {
        if (!mGizmoDragging) {
            // One capture at the start of the drag: the whole drag becomes a
            // single undo entry, not one per frame.
            mGizmoDragging = true;
            mDragStart.clear();
            for (const AuthorId& id : mState.selection)
                if (const Entity* entity = mState.document.find(id))
                    mDragStart.emplace_back(id, entity->transform);
        }
        glm::vec3 position, rotation, scale;
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(matrix), glm::value_ptr(position),
            glm::value_ptr(rotation), glm::value_ptr(scale));
        // The gizmo reports where the ANCHOR moved to; the entities move by the
        // same delta. Assigning the gizmo's position straight onto the primary
        // would teleport it to the bounds centre on the first frame of a drag.
        const glm::vec3 delta = position - anchor;
        for (const AuthorId& id : mState.selection) {
            Entity* entity = mState.document.find(id);
            if (!entity)
                continue;
            entity->transform.position += delta;
            if (id == primary->id) {
                entity->transform.rotationDegrees = rotation;
                entity->transform.scale = scale;
            }
        }
        mState.document.touch();
    } else if (mGizmoDragging) {
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
    }

    // Selection outline: a box around what the gizmo is acting on, so the
    // handles are never ambiguous about their subject.
    if (haveBounds) {
        const glm::mat4 viewProjection = projection * view;
        const auto project = [&](const glm::vec3& world, ImVec2& out) {
            const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
            if (clip.w <= 1e-4f)
                return false;
            out = ImVec2(mViewportX + (clip.x / clip.w * 0.5f + 0.5f) * mViewportW,
                         mViewportY + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) *
                                          mViewportH);
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
bool EditorApp::hoveredPlacement(const eng::FrameContext& f, CellPlacement& cell,
                                 XformAuthor& transform) const
{
    if (mState.brushPrefab.empty() || mViewportW < 8.0f)
        return false;
    const KitPiece* piece = mState.catalog.find(mState.brushPrefab);
    if (!piece)
        return false;

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const glm::vec2 ndc{(mouse.x - mViewportX) / mViewportW * 2.0f - 1.0f,
                        1.0f - (mouse.y - mViewportY) / mViewportH * 2.0f};
    const Ray ray = screenRay(ndc, mState.camera.flyEye(),
                              mState.camera.flyOrientation(),
                              glm::radians(65.0f), mViewportW / mViewportH);
    glm::vec3 hit;
    if (!rayPlaneY(ray, mState.gridState.level, hit))
        return false;

    cell = CellPlacement{};
    cell.level = mState.gridState.level;
    cell.span = piece->span;
    cell.yawQuarters = mBrushYawQuarters;

    if (socketUsesGrid(piece->socket)) {
        if (piece->socket == Socket::Wall || piece->socket == Socket::Opening) {
            // Snapped to the nearest grid LINE, so the ghost stays put along
            // the length of a wall instead of flipping edges mid-stroke.
            nearestWallSlot(mState.grid, hit, cell.col, cell.row, cell.edge);
        } else {
            pointToCell(mState.grid, hit, cell.col, cell.row);
        }
        transform = placementToTransform(mState.grid, mState.catalog, *piece,
                                         cell);
    } else {
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

    // One piece per slot per drag: painting across a cell twice must not stack
    // two floors in it.
    std::string slot = std::to_string(cell.col) + ',' + std::to_string(cell.row) +
                       ',' + std::to_string(int(cell.edge)) + ',' +
                       std::to_string(int(std::lround(cell.level * 100.0f)));
    if (!socketUsesGrid(piece->socket)) {
        // Free objects have no slot; every click is a new one.
        slot = "free," + std::to_string(mPaintParts.size());
    }
    for (const std::string& painted : mPaintedSlots)
        if (painted == slot) return;
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

void EditorApp::drawPlacementGhost(const eng::FrameContext& f)
{
    if (mState.tool != Tool::Place || !mViewportHovered || mFlying)
        return;

    CellPlacement cell;
    XformAuthor transform;
    if (!hoveredPlacement(f, cell, transform))
        return;
    const KitPiece* piece = mState.catalog.find(mState.brushPrefab);
    if (!piece)
        return;

    // The ghost is a wire box in screen space rather than a translucent mesh:
    // it needs no material, cannot be picked, and reads clearly against any
    // geometry behind it.
    glm::vec3 localMin, localMax;
    piece->localBoundsMeters(mState.catalog.scale(), localMin, localMax);
    const float yaw = glm::radians(transform.rotationDegrees.y);
    const glm::quat rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 view, projection;
    cameraMatrices(mState.camera, mViewportW / mViewportH, view, projection);
    const glm::mat4 viewProjection = projection * view;

    const auto project = [&](const glm::vec3& local, ImVec2& out) {
        const glm::vec3 world = transform.position + rotation * local;
        const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
        if (clip.w <= 1e-4f)
            return false;
        out = ImVec2(mViewportX + (clip.x / clip.w * 0.5f + 0.5f) * mViewportW,
                     mViewportY + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) *
                                      mViewportH);
        return true;
    };

    const glm::vec3 corners[8] = {
        {localMin.x, localMin.y, localMin.z}, {localMax.x, localMin.y, localMin.z},
        {localMax.x, localMin.y, localMax.z}, {localMin.x, localMin.y, localMax.z},
        {localMin.x, localMax.y, localMin.z}, {localMax.x, localMax.y, localMin.z},
        {localMax.x, localMax.y, localMax.z}, {localMin.x, localMax.y, localMax.z},
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
    if (!mStage.built() || mViewportW < 8.0f)
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
    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                         ImGuizmo::ROTATE, ImGuizmo::WORLD,
                         glm::value_ptr(matrix));
    if (ImGuizmo::IsUsing()) {
        glm::vec3 outPosition, outRotation, outScale;
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(matrix), glm::value_ptr(outPosition),
            glm::value_ptr(outRotation), glm::value_ptr(outScale));
        mStageAutoSpin = false; // dragging takes over from the turntable
        mStage.setSpin(f.engine.renderer(), glm::radians(outRotation.y));
    }
}


// --- room tool ---------------------------------------------------------------

bool EditorApp::hoveredCell(int& col, int& row) const
{
    if (mViewportW < 8.0f)
        return false;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const glm::vec2 ndc{(mouse.x - mViewportX) / mViewportW * 2.0f - 1.0f,
                        1.0f - (mouse.y - mViewportY) / mViewportH * 2.0f};
    const Ray ray = screenRay(ndc, mState.camera.flyEye(),
                              mState.camera.flyOrientation(),
                              glm::radians(65.0f), mViewportW / mViewportH);
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
        const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
        if (clip.w <= 1e-4f)
            return false;
        out = ImVec2(mViewportX + (clip.x / clip.w * 0.5f + 0.5f) * mViewportW,
                     mViewportY + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) *
                                      mViewportH);
        return true;
    };

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float cell = mState.grid.cell;
    const float level = spec.level;
    const glm::vec3 min = cellCentre(mState.grid, spec.minCol(), spec.minRow(),
                                     level) - glm::vec3(cell * 0.5f, 0.0f,
                                                        cell * 0.5f);
    const glm::vec3 max = cellCentre(mState.grid, spec.maxCol(), spec.maxRow(),
                                     level) + glm::vec3(cell * 0.5f, 0.0f,
                                                        cell * 0.5f);

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
        if (const KitPiece* wall = mState.catalog.find(mState.roomSpec.wallPrefab)) {
            const float height = wall->sizeMeters(mState.catalog.scale()).y;
            for (int i = 0; i < 4; ++i) {
                ImVec2 top;
                glm::vec3 up = floorCorners[i];
                up.y += height;
                if (project(up, top))
                    draw->AddLine(quad[i], top, IM_COL32(140, 220, 255, 140), 1.5f);
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

// A one-word tag for what an entity IS, so a list of a hundred pieces can be
// scanned rather than read. Kit prefabs get their socket, everything else gets
// the gameplay role that made it exist.
static const char* entityKind(const Entity& entity, const KitCatalog& catalog)
{
    if (entity.playerSpawn) return "spawn";
    if (entity.exitYawDegrees) return "exit";
    if (entity.enemySpawn) return "enemy";
    if (entity.pickup) return "pickup";
    if (entity.trigger) return "trigger";
    if (entity.light)
        return entity.light->type == LightAuthor::Type::Directional ? "sun"
                                                                    : "light";
    if (entity.marker) return "marker";
    if (!entity.prefab.empty()) {
        if (const KitPiece* piece = catalog.find(entity.prefab))
            return socketName(piece->socket);
        return "MISSING";
    }
    if (entity.collider) return "volume";
    return "node";
}

static ImVec4 kindColour(const char* kind)
{
    const std::string k = kind;
    if (k == "MISSING") return ImVec4(1.00f, 0.42f, 0.36f, 1.0f);
    if (k == "spawn" || k == "exit") return ImVec4(0.55f, 0.92f, 0.62f, 1.0f);
    if (k == "enemy" || k == "trigger") return ImVec4(1.00f, 0.68f, 0.45f, 1.0f);
    if (k == "light" || k == "sun") return ImVec4(0.98f, 0.88f, 0.45f, 1.0f);
    if (k == "marker" || k == "pickup") return ImVec4(0.68f, 0.78f, 1.00f, 1.0f);
    return ImVec4(0.60f, 0.63f, 0.70f, 1.0f); // kit geometry: the quiet majority
}

void EditorApp::drawOutliner()
{
    if (!ImGui::Begin("Outliner", nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##outlinerfilter", "filter by name, id or kind",
                             mOutlinerFilter, sizeof(mOutlinerFilter));
    const std::string filter = mOutlinerFilter;

    // Geometry is most of a level by count and the least interesting to click,
    // so it can be folded away to leave the gameplay entities visible.
    ImGui::Checkbox("show geometry", &mOutlinerShowGeometry);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu entities", mState.document.entities.size());

    ImGui::Separator();
    if (ImGui::BeginChild("##entities")) {
        int shown = 0;
        for (const Entity& entity : mState.document.entities) {
            const char* kind = entityKind(entity, mState.catalog);
            const bool geometry = !entity.prefab.empty() && !entity.light &&
                                  !entity.marker && !entity.playerSpawn &&
                                  !entity.exitYawDegrees && !entity.enemySpawn &&
                                  !entity.pickup && !entity.trigger;
            if (geometry && !mOutlinerShowGeometry)
                continue;
            const std::string label =
                entity.name.empty() ? entity.id : entity.name;
            if (!filter.empty() &&
                label.find(filter) == std::string::npos &&
                entity.id.find(filter) == std::string::npos &&
                std::string(kind).find(filter) == std::string::npos)
                continue;
            ++shown;

            ImGui::PushID(entity.id.c_str());
            const bool selected = mState.isSelected(entity.id);
            if (ImGui::Selectable("##row", selected,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::GetIO().KeyShift)
                    mState.toggleSelected(entity.id);
                else
                    mState.select(entity.id);
                // Double-click frames it, the way it does in every outliner.
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    frameSelectionOrAll();
            }
            // Right-click acts on the row under the cursor, selecting it first
            // so the menu can never act on something else.
            if (ImGui::BeginPopupContextItem("##ctx")) {
                if (!mState.isSelected(entity.id))
                    mState.select(entity.id);
                if (ImGui::MenuItem("Focus", "F"))
                    frameSelectionOrAll();
                if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                    duplicateSelection();
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", "Del"))
                    deleteSelection();
                ImGui::EndPopup();
            }
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(kindColour(kind), "%-7s", kind);
            ImGui::SameLine();
            ImGui::TextUnformatted(label.c_str());
            ImGui::PopID();
        }
        if (shown == 0)
            ImGui::TextDisabled("nothing matches");
    }
    ImGui::EndChild();
    ImGui::End();
}

// A point in front of the camera on the work plane: where a new gameplay
// entity should appear. Dropping them at the origin instead means the author
// has to go find every one of them.
glm::vec3 EditorApp::viewFocusPoint() const
{
    Ray ray;
    ray.origin = mState.camera.flyEye();
    ray.dir = mState.camera.flyOrientation() * glm::vec3(0.0f, 0.0f, -1.0f);
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

    const char* stem = "entity";
    switch (kind) {
    case Gameplay::PlayerSpawn:
        stem = "player_spawn";
        entity.playerSpawn = true;
        break;
    case Gameplay::Exit:
        stem = "exit";
        entity.exitYawDegrees = 0.0f;
        break;
    case Gameplay::Marker:
        stem = "marker";
        entity.marker = "group.name";
        break;
    case Gameplay::EnemySpawn:
        stem = "enemy";
        entity.enemySpawn = "goblin";
        break;
    case Gameplay::Pickup:
        stem = "pickup";
        entity.pickup = "potion";
        break;
    case Gameplay::Trigger:
        stem = "trigger";
        entity.trigger = TriggerAuthor{{2.0f, 2.0f, 2.0f}, "event.name"};
        break;
    case Gameplay::PointLight:
        stem = "light";
        entity.transform.position.y += 3.0f;
        entity.light = LightAuthor{LightAuthor::Type::Point,
                                   {1.0f, 0.75f, 0.45f}, 8.0f, false};
        break;
    case Gameplay::DirectionalLight:
        stem = "key_light";
        entity.transform.position.y += 8.0f;
        entity.transform.rotationDegrees = {-55.0f, 30.0f, 0.0f};
        entity.light = LightAuthor{LightAuthor::Type::Directional,
                                   {0.95f, 0.93f, 0.88f}, 0.0f, true};
        break;
    }
    entity.id = mState.document.allocateId(stem);
    entity.name = entity.id;

    runCommand(makeCreateEntity(entity));
    mState.select(entity.id);
    mPreview->invalidate();
}

void EditorApp::drawCatalog()
{
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
            button("player spawn", Gameplay::PlayerSpawn);
            button("exit", Gameplay::Exit);
            button("marker", Gameplay::Marker);
            button("enemy spawn", Gameplay::EnemySpawn);
            button("pickup", Gameplay::Pickup);
            button("trigger volume", Gameplay::Trigger);
            button("point light", Gameplay::PointLight);
            button("directional light", Gameplay::DirectionalLight);
            ImGui::Spacing();
            if (ImGui::Button("add collider to selection", ImVec2(-1.0f, 0.0f))) {
                if (const AuthorId* id = mState.primary()) {
                    if (const Entity* entity = mState.document.find(*id)) {
                        Entity updated = *entity;
                        if (!updated.collider)
                            updated.collider =
                                ColliderAuthor{{1.0f, 1.0f, 1.0f}, {}};
                        runCommand(makeEditEntity("add collider", *id, *entity,
                                                  updated));
                    }
                }
            }
        }

        if (ImGui::BeginChild("##pieces")) {
            const std::string filter = mCatalogFilter;
            // Grouped by role, which is how kit.toml is authored and how an
            // author thinks: "I need a wall", not "I need piece 17".
            for (const std::string& role : mState.catalog.roles()) {
                std::vector<const KitPiece*> pieces = mState.catalog.byRole(role);
                std::vector<const KitPiece*> shown;
                for (const KitPiece* piece : pieces) {
                    if (filter.empty() ||
                        piece->id.find(filter) != std::string::npos ||
                        role.find(filter) != std::string::npos)
                        shown.push_back(piece);
                }
                if (shown.empty())
                    continue;
                if (!ImGui::CollapsingHeader(role.c_str(),
                                             filter.empty()
                                                 ? 0
                                                 : ImGuiTreeNodeFlags_DefaultOpen))
                    continue;
                for (const KitPiece* piece : shown) {
                    const bool active = mState.brushPrefab == piece->id;
                    // Strip the "kit." for display; the id still carries it.
                    const char* label = piece->id.c_str() + 4;
                    if (ImGui::Selectable(label, active)) {
                        mState.brushPrefab = piece->id;
                        mState.tool = Tool::Place;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\nsocket %s  span %d\n%s",
                                          piece->id.c_str(),
                                          socketName(piece->socket), piece->span,
                                          piece->meshPath.c_str());
                    }
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void EditorApp::drawInspector()
{
    if (!ImGui::Begin("Inspector", nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }
    const AuthorId* primary = mState.primary();
    Entity* entity = primary ? mState.document.find(*primary) : nullptr;
    if (!entity) {
        ImGui::TextUnformatted("nothing selected");
        ImGui::TextDisabled("click something in the viewport or the outliner");
        ImGui::End();
        return;
    }
    if (mState.selection.size() > 1) {
        ImGui::TextDisabled("%zu selected -- editing '%s'",
                            mState.selection.size(), entity->id.c_str());
        ImGui::Separator();
    }

    // Fields are mutated live so the viewport follows the drag, and the command
    // is recorded when the widget is released -- one undo entry per edit, not
    // one per frame.
    const Entity before = *entity;
    bool edited = false;
    bool closed = false;
    const auto track = [&] {
        edited = edited || ImGui::IsItemEdited();
        closed = closed || ImGui::IsItemDeactivatedAfterEdit();
    };

    ImGui::Text("id      %s", entity->id.c_str());
    char name[128];
    std::snprintf(name, sizeof(name), "%s", entity->name.c_str());
    if (ImGui::InputText("name", name, sizeof(name)))
        entity->name = name;
    track();

    if (!entity->prefab.empty()) {
        const KitPiece* piece = mState.catalog.find(entity->prefab);
        ImGui::Text("prefab  %s", entity->prefab.c_str());
        // Resolver state, visible: a fallback is an authoring signal, not a
        // silent convenience.
        if (piece) {
            ImGui::TextDisabled("mesh    %s", piece->meshPath.c_str());
            ImGui::TextDisabled("socket  %s  span %d", socketName(piece->socket),
                                piece->span);
            // Material override. Empty means the kit piece's own, which is what
            // nearly everything should use; the override is for the one-off.
            const std::string current = entity->material.empty()
                                            ? piece->material + "  (from kit)"
                                            : entity->material;
            if (ImGui::BeginCombo("material", current.c_str())) {
                if (ImGui::Selectable("(from kit)", entity->material.empty())) {
                    entity->material.clear();
                    edited = closed = true;
                }
                for (const std::string& option : mMaterialNames) {
                    if (ImGui::Selectable(option.c_str(),
                                          option == entity->material)) {
                        entity->material = option;
                        edited = closed = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                               "mesh    UNRESOLVED");
        }
    }

    ImGui::SeparatorText("transform");
    ImGui::DragFloat3("position", &entity->transform.position.x, 0.05f);
    track();
    ImGui::DragFloat3("rotation", &entity->transform.rotationDegrees.x, 1.0f);
    track();
    ImGui::DragFloat3("scale", &entity->transform.scale.x, 0.01f, 0.001f, 100.0f);
    track();

    if (entity->cell) {
        ImGui::SeparatorText("grid");
        ImGui::Text("cell %d,%d  edge %d  span %d", entity->cell->col,
                    entity->cell->row, int(entity->cell->edge),
                    entity->cell->span);
        if (ImGui::Button("snap to cell")) {
            if (const KitPiece* piece = mState.catalog.find(entity->prefab)) {
                entity->transform = placementToTransform(
                    mState.grid, mState.catalog, *piece, *entity->cell);
                edited = closed = true;
            }
        }
    }

    if (entity->light) {
        ImGui::SeparatorText("light");
        int type = entity->light->type == LightAuthor::Type::Directional ? 0 : 1;
        if (ImGui::Combo("type", &type, "directional\0point\0"))
            entity->light->type = type == 0 ? LightAuthor::Type::Directional
                                            : LightAuthor::Type::Point;
        track();
        ImGui::ColorEdit3("colour", &entity->light->colour.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        track();
        if (entity->light->type == LightAuthor::Type::Point) {
            ImGui::DragFloat("range", &entity->light->range, 0.25f, 0.0f, 200.0f);
            track();
        }
        ImGui::Checkbox("cast shadows", &entity->light->castShadows);
        track();
    }

    if (entity->collider) {
        ImGui::SeparatorText("collider");
        ImGui::DragFloat3("half extents", &entity->collider->halfExtents.x, 0.05f,
                          0.0f, 100.0f);
        track();
        ImGui::DragFloat3("offset", &entity->collider->offset.x, 0.05f);
        track();
        if (ImGui::SmallButton("remove collider")) {
            entity->collider.reset();
            edited = closed = true;
        }
    }

    ImGui::SeparatorText("gameplay");
    if (ImGui::Checkbox("player spawn", &entity->playerSpawn))
        edited = closed = true;
    const auto stringField = [&](const char* label,
                                 std::optional<std::string>& field) {
        if (!field)
            return;
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "%s", field->c_str());
        if (ImGui::InputText(label, buffer, sizeof(buffer)))
            *field = buffer;
        track();
    };
    stringField("marker", entity->marker);
    stringField("enemy", entity->enemySpawn);
    stringField("pickup", entity->pickup);
    if (entity->exitYawDegrees) {
        ImGui::DragFloat("exit yaw", &*entity->exitYawDegrees, 1.0f);
        track();
    }
    if (entity->trigger) {
        ImGui::DragFloat3("trigger size", &entity->trigger->size.x, 0.05f, 0.0f,
                          50.0f);
        track();
        char event[96];
        std::snprintf(event, sizeof(event), "%s", entity->trigger->event.c_str());
        if (ImGui::InputText("event", event, sizeof(event)))
            entity->trigger->event = event;
        track();
    }
    if (!entity->castShadows || !entity->prefab.empty()) {
        if (ImGui::Checkbox("cast shadows", &entity->castShadows))
            edited = closed = true;
    }

    if (closed) {
        // The command captures the whole entity before and after -- small enough
        // to copy, and it means one code path per widget instead of one command
        // type per field.
        runCommand(makeEditEntity("edit " + entity->id, entity->id, before,
                                  *entity));
    }
    if (edited)
        mState.document.touch();
    ImGui::End();
}

void EditorApp::drawIssues()
{
    if (ImGui::Begin("Issues", nullptr, kPanelFlags)) {
        // Revalidated when the document changes rather than every frame: it
        // walks every entity and the panel is often open while dragging.
        if (mIssuesRevision != mState.document.revision) {
            mIssuesRevision = mState.document.revision;
            mIssues = validate(mState.document, mState.catalog, mState.assetRoot);
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
                const ImVec4 colour =
                    issue.severity == Severity::Error
                        ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                        : ImVec4(0.95f, 0.82f, 0.38f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, colour);
                const std::string label =
                    issue.code + "##issue" + std::to_string(i);
                if (ImGui::Selectable(label.c_str()) && !issue.entity.empty()) {
                    mState.select(issue.entity);
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
                        // other edit, so a fix that was wrong is one Ctrl+Z away.
                        const Entity* target = issue.entity.empty()
                                                   ? nullptr
                                                   : mState.document.find(
                                                         issue.entity);
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
                            } else {
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
    eng::Renderer& renderer = mEngine->renderer();

    if (material) {
        if (!mStage.built()) {
            mStage.build(renderer);
            mMaterialNames = renderer.materialNames();
            std::sort(mMaterialNames.begin(), mMaterialNames.end());
            mStage.setMaterial(renderer, mStage.material());
        }
        mStage.setVisible(renderer, true);
        mStage.applyEnvironment(renderer);
        // The level is hidden rather than unloaded: switching modes must not
        // cost a rebuild, and must never touch the document.
        mPreview->setVisible(renderer, false);
        mCameraBeforeMode = mState.camera;
        mState.camera.setFlyPosition(mStage.cameraPosition());
        mState.camera.setYawPitch(mStage.cameraYaw(), mStage.cameraPitch());
        mState.camera.frame(mStage.focusPoint(), 4.0f);
    } else {
        mStage.setVisible(renderer, false);
        mPreview->setVisible(renderer, true);
        applySceneEnvironment(renderer);
        mState.camera = mCameraBeforeMode;
    }
}

void EditorApp::drawMaterialPanel()
{
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
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            mThumbAutoSpin = false;
            mStage.spinThumbnail(renderer,
                                 mStage.thumbnailSpin() +
                                     ImGui::GetIO().MouseDelta.x * 0.01f);
        }
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::TextUnformatted(mSelectedMaterial.empty() ? "(no material)"
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
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Shows one material on a sphere over a reference floor, lit by the\n"
            "game's own shaders -- not a PBR preview, because the game does not\n"
            "render PBR. What you see here is what the dungeon will show.");
    }

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
    if (ImGui::BeginChild("##materials")) {
        const std::string filter = mMaterialFilter;
        for (const std::string& name : mMaterialNames) {
            if (!filter.empty() && name.find(filter) == std::string::npos)
                continue;
            if (ImGui::Selectable(name.c_str(), name == mSelectedMaterial)) {
                mSelectedMaterial = name;
                mStage.setThumbnailMaterial(renderer, name);
                if (mMaterialMode)
                    mStage.setMaterial(renderer, name);
            }
            // Hovering previews. Selecting commits. That split is what makes
            // scrubbing a long list to find the right material actually work.
            if (ImGui::IsItemHovered())
                mStage.setThumbnailMaterial(renderer, name);
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

// Sets a per-entity material override on everything selected. The kit piece's
// own material stays the default for every OTHER instance -- this is the
// one-off, not a way to restyle a whole level (kit.toml is that).
void EditorApp::applyMaterialToSelection(const std::string& material)
{
    if (material.empty() || mState.selection.empty())
        return;
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
}

// Save As. A plain path field rather than a file browser: the scenes all live
// in one directory, and a browser is a lot of UI for choosing between six files.
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
        ImGui::Text("%s%s | cook: %s | undo: %s",
                    mState.scenePath.empty()
                        ? "(no scene)"
                        : std::filesystem::path(mState.scenePath)
                              .filename()
                              .string()
                              .c_str(),
                    mState.dirty ? " *" : "", mCookStatus.c_str(),
                    mCommands.canUndo() ? mCommands.undoLabel().c_str()
                                        : "(empty)");
        ImGui::Text("camera %.1f %.1f %.1f | %zu batches, %zu tris",
                    double(mState.camera.flyEye().x),
                    double(mState.camera.flyEye().y),
                    double(mState.camera.flyEye().z), mBatches, mTriangles);
    }
    ImGui::End();
}

void EditorApp::onShutdown(eng::Engine&)
{
    mPreview.reset();
}

} // namespace ed
