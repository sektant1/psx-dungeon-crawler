#include <editor/app/EditorApp.h>

#include <editor/scene/Attachments.h>
#include <editor/ui/ComponentInspector.h>
#include <editor/ui/EditorWorkspace.h>
#include <editor/ui/OutlinerPanel.h>
#include <editor/scene/MultiEdit.h>
#include <editor/scene/PaintSlot.h>
#include <editor/scene/Picker.h>
#include <editor/scene/PickTarget.h>
#include <editor/viewport/ViewportGrid.h>

#include <optional>
#include <editor/content/SceneCook.h>

#include <algorithm>
#include <editor/content/SceneSource.h>
#include <editor/content/SceneTemplates.h>
#include <editor/content/SceneContract.h>
#include <editor/content/SceneValidate.h>

#include <scene/ComponentRegistry.h> // mapio::coreRegistry(), for the Timeline
#include <editor/content/SceneWriter.h>

#include <eng/Input.h>
#include <eng/Audio.h>
#include <eng/Log.h>
#include <eng/telemetry/Telemetry.h>
#include <eng/render/Warmup.h>
#include <eng/render/ImGuiHint.h>
#include <eng/render/ImGuiTheme.h>
#include <eng/Renderer.h>
#include <eng/assets/AssetName.h>
#include <eng/assets/AssetRoot.h>

#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_internal.h> // DockBuilderGetNode: preserve a restored workspace

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>
#include <nlohmann/json.hpp>

namespace ed {
namespace {

using namespace game::content;

// What the editor opens when the command line names no .scn. Held as a bare
// filename because configure() -- which needs it for the loading hint -- runs
// before the resolver is mounted and cannot turn it into a path yet.
// The scene the editor opens with no file named. The turntable is the model
// showroom: a small vaulted chamber, three stage lights, a framed camera, and
// one model turning on the plinth. It opens by default because it is the scene
// most sessions start from -- put an asset on the plinth and look at it -- and
// because everything in it except the subject is meant to stay put, so an
// accidental save costs nothing. tools/author_turntable.py regenerates it.
constexpr const char* kDefaultScene = "turntable.scn";

// Grid drawn as world-space debug lines rather than an ImGui overlay: it has to
// sit *under* the geometry and take perspective, which a 2D draw list cannot
// do.
constexpr int kGridRadius = 16; // cells drawn either side of the camera

// How far out the placement ghost sits when the cursor points above the
// horizon, in metres. Roughly a room away: near enough to aim, far enough that
// the piece does not sit in the camera's face.
constexpr float kGhostFallbackDistance = 12.0f;

// Every dockable panel's flags.
//
// HorizontalScrollbar is the backstop against content that cannot be reached.
// Dear ImGui clips whatever runs past a window's right edge and, without this,
// offers no way to scroll to it -- so an Inspector docked narrow lost the right
// half of its transform rows, a wide table lost its last column, and neither
// said so. Panels should still be built to wrap (see ed::ui::sameLineIfItFits);
// this is what makes the cases that cannot wrap survivable rather than silent.
constexpr ImGuiWindowFlags kPanelFlags =
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_HorizontalScrollbar;

const ImVec4 kUiWarning(0.780f, 0.604f, 0.314f, 1.0f);
const ImVec4 kUiDanger(0.890f, 0.416f, 0.333f, 1.0f);

// filterMatches moved to ed::ui: all four asset tabs, the outliner and the
// command palette were each carrying their own copy of "does this row match
// what was typed", and two of them disagreed about case.

// The one place the editor decides how wide its view is. Walk mode deliberately
// uses the game's field of view rather than the editor's, because framing the
// same amount of world the player will see is the entire point of that mode --
// and every consumer has to agree on it, or the picker builds rays for a
// frustum the viewport is not drawing.
float viewportFovDeg(const EditorCamera& camera)
{
    return camera.activeFovDeg();
}

// One-shot effects need to replay while they are being inspected or carried by
// the placement cursor. Long enough for the final particle to die, capped so a
// single impact never leaves a seemingly empty preview for several seconds.
float particlePreviewPeriod(const eng::ParticleEffectDesc& effect)
{
    if (effect.loop)
        return 0.0f;
    float ttl = 0.0f;
    for (const eng::ParticleEmitterDesc& emitter : effect.emitters)
        ttl = std::max(ttl, emitter.ttlMax);
    return std::clamp(0.25f + ttl, 0.45f, 2.0f);
}

// transformedBounds lives in DocumentRaycast.h now: picking, framing and
// placement all need it, and placement needs the ray test it feeds.

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
    mState.assetRoot = eng::assets::packDir("content").string();
    mState.kitPath = eng::assets::resolve("config/kit.toml").string();
    // Where the model picker looks first. assets/source is the unbuilt art --
    // .glb, .blend, .fbx -- as opposed to the pack, which holds what the cooker
    // has already turned into engine OBJ parts.
    mImportScanRoot = (eng::assets::project() / "assets" / "source").string();
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

    mAudio = std::make_unique<eng::Audio>();
    if (!mAudio->startup())
        eng::log::warn("Editor audio preview unavailable");
    refreshAudioAssets();

    if (std::getenv("RAVEN_EDITOR_SELFTEST"))
        mSelfTestStep = 0;

    installConsoleCommands();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Numeric inspector fields support both workflows: drag to scrub, or a
    // click-release to type an exact value. Requiring Ctrl+click made the
    // anonymous DragFloat controls feel like sliders with no text-entry path.
    io.ConfigDragClickToInputText = true;

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
    mRecentFile =
        (eng::assets::project() / "artifacts" / "editor" / "recent.txt")
            .string();
    mRecent.load(mRecentFile);
    mSettingsFile =
        (eng::assets::project() / "artifacts" / "editor" / "settings.txt")
            .string();
    mSettings = loadEditorSettings(mSettingsFile);
    mAutosaveIn = mSettings.autosaveSeconds;
    mProjectRecentsFile =
        (eng::assets::project() / "artifacts" / "editor" / "projects.txt")
            .string();
    mProject.loadRecents(mProjectRecentsFile);

    // The ids the game defines, so a spawn is picked from a list rather than
    // spelled from memory. Resolved through the mount list like any other
    // content; a missing file leaves the field free text.
    if (const std::filesystem::path enemies =
            eng::assets::resolve("config/enemies.toml");
        !enemies.empty()) {
        mEnemyIds = enemyIdsFromToml(enemies.string());
        eng::log::info("Editor: %zu enemy ids", mEnemyIds.size());
    }
    // Items, for the Pickup component's picker.
    //
    // This read config/prototypes.toml and asked it for a `[pickup.*]` section.
    // That file holds the missing-asset substitution rules -- `[[mesh]]` and
    // `[[material]]` -- and has never had a `[pickup]` table, so the list was
    // always empty: the editor offered a Pickup entity and no id to put in it,
    // which made the component unauthorable. items.toml is where items live.
    if (const std::filesystem::path items =
            eng::assets::resolve("config/items.toml");
        !items.empty()) {
        mPickupIds = tomlSectionIds(items.string(), "item");
        eng::log::info("Editor: %zu item ids", mPickupIds.size());
    }
    // The safehouse/village projects and the traders, for the same reason: a
    // marker naming one of these is a string you can only get right by having
    // read a TOML.
    if (const std::filesystem::path stations =
            eng::assets::resolve("config/stations.toml");
        !stations.empty())
        mStationIds = tomlSectionIds(stations.string(), "station");
    if (const std::filesystem::path traders =
            eng::assets::resolve("config/traders.toml");
        !traders.empty())
        mTraderIds = tomlSectionIds(traders.string(), "trader");
    if (const std::filesystem::path quests =
            eng::assets::resolve("config/quests.toml");
        !quests.empty())
        mQuestIds = tomlSectionIds(quests.string(), "quest");
    // The roster, which is the vocabulary the NPC component is authored
    // against. Deliberately npcs.toml and not dialogue.toml: a shopkeeper with
    // stock and no conversation written yet is somebody you can still stand in
    // the village, and a picker sourced from the conversations would have been
    // the one list that refused to offer them.
    if (const std::filesystem::path npcs =
            eng::assets::resolve("config/npcs.toml");
        !npcs.empty())
        mNpcIds = tomlSectionIds(npcs.string(), "npc");
    eng::log::info("Editor: %zu npc ids, %zu traders, %zu quests, %zu stations",
                   mNpcIds.size(), mTraderIds.size(), mQuestIds.size(),
                   mStationIds.size());
    // The player loadout, for the viewmodel preview's weapon picker. Same
    // argument as enemies and pickups: an id you can only get right by having
    // read a TOML is not authorable.
    if (const std::filesystem::path weapons =
            eng::assets::resolve("config/weapons.toml");
        !weapons.empty())
        mWeaponIds = tomlSectionIds(weapons.string(), "player_weapon");
    rescanScriptPaths();
    if (const std::filesystem::path palettes =
            eng::assets::resolve("config/palettes.toml");
        !palettes.empty()) {
        mPalettesPath = palettes.string();
        mPalettes = tomlSectionIds(mPalettesPath, "palette");
        eng::log::info("Editor: %zu palettes", mPalettes.size());
    }

    // Preferences are loaded above and palette metadata is now available, so
    // applying game lighting here cannot use the defaults and overwrite the
    // loaded values on the first captureSettings() frame.
    applySettings();

    if (!mPendingScene.empty() && loadScene(mPendingScene)) {
        mRecent.touch(mPendingScene);
        mRecent.save(mRecentFile);
        if (autosaveIsStale(mPendingScene, mState.assetRoot + "/scenes"))
            mStatus += "  |  a newer autosave exists -- Scene > Recover "
                       "autosave";
    }
    // The tab set starts with one empty tab; the scene just loaded into mState
    // has to be folded back into it, or the first tab switch would restore the
    // blank document over the author's level.
    captureActiveTab();
    // The contract decides which editor a scene opens in -- a 2D page in the 3D
    // view is a page seen edge-on.
    setMainScreen(mainScreenForKind(contract().kind));
    if (const char* import = std::getenv("RAVEN_EDITOR_IMPORT_MODEL"))
        importModel(import);
    // Verification hook: start in the staging scene so a screenshot run can
    // capture it without driving the UI.
    if (std::getenv("RAVEN_EDITOR_MATERIAL"))
        setMainScreen(MainScreen::Material);
    // The same hook for the 2D screen, which is where UI scenes are authored:
    // a screenshot run has no way to click a tab, and "does the screen I just
    // authored actually draw" is the one question worth capturing.
    if (std::getenv("RAVEN_EDITOR_SCREEN2D"))
        setMainScreen(MainScreen::Screen2D);
    // The same hook for isolation: a screenshot run can enter the mode on a
    // named entity without anybody driving the outliner.
    if (const char* unpack = std::getenv("RAVEN_EDITOR_UNPACK"))
        unpackAttachments(unpack);
    if (const char* isolate = std::getenv("RAVEN_EDITOR_ISOLATE"))
        enterIsolation(isolate);
    // Verification hook: stage one material by name. Without it a screenshot
    // run can only cycle the whole list and hope, which cannot show a specific
    // rig (the quad is only reachable through a handful of names).
    if (const char* pick = std::getenv("RAVEN_EDITOR_MATERIAL_NAME")) {
        mSelectedMaterial = pick;
        if (mStage.built())
            mStage.setMaterial(engine.renderer(), mSelectedMaterial);
    }
    // Verification hooks: drive the two interactions a screenshot run cannot
    // click on its own.
    if (std::getenv("RAVEN_EDITOR_CYCLE_MATERIALS"))
        mCycleMaterials = true;
    // The viewport's projection. Same argument as the panel hook below: the
    // elevations are a toolbar click, and a screenshot run has no mouse.
    if (const char* view = std::getenv("RAVEN_EDITOR_VIEW")) {
        const std::string which = view;
        if (which == "top")
            mPendingView = EditorCamera::Projection::Top;
        else if (which == "front")
            mPendingView = EditorCamera::Projection::Front;
        else if (which == "side")
            mPendingView = EditorCamera::Projection::Side;
    }
    // Verification hook: the surfaces that only exist while a key is held or a
    // menu is open, and that a screenshot run therefore cannot reach.
    if (const char* panel = std::getenv("RAVEN_EDITOR_PANEL")) {
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
    if (const char* query = std::getenv("RAVEN_EDITOR_PALETTE_QUERY")) {
        openPalette(mPalette);
        std::snprintf(mPalette.query, sizeof(mPalette.query), "%s", query);
    }
    // Verification hook: build a room without a mouse, so a screenshot run can
    // show what the tool produces.
    if (const char* room = std::getenv("RAVEN_EDITOR_DEMO_ROOM")) {
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
    if (const char* select = std::getenv("RAVEN_EDITOR_SELECT")) {
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
    if (std::getenv("RAVEN_EDITOR_WALK"))
        toggleWalk();
    // Verification hook: arm the Place tool with a piece, so a capture can show
    // the placement ghost -- which otherwise needs a click in the Catalog.
    if (const char* brush = std::getenv("RAVEN_EDITOR_BRUSH")) {
        if (mState.catalog.find(brush)) {
            mState.brush.kind = Brush::Kind::Piece;
            mState.brush.prefab = brush;
            mState.tool = Tool::Place;
        }
    }
    // Verification hook: give the selection a component without the mouse. Runs
    // the same path the inspector's Add Component does, so a capture shows what
    // an author would get.
    if (const char* add = std::getenv("RAVEN_EDITOR_ADD_COMPONENT"))
        if (const ComponentType* type = findComponentType(add))
            addComponentToSelection(*type);

    return true;
}

void EditorApp::rescanScriptPaths()
{
    // Through the mount list like every other content lookup, so the picker
    // offers exactly what the cooker and the runtime will resolve.
    const std::filesystem::path dir = eng::assets::resolve("scripts");
    mScriptPaths = luaScriptPaths(dir.string(), "scripts");
    eng::log::info("Editor: %zu scripts", mScriptPaths.size());
}

void EditorApp::refreshAudioAssets()
{
    mAudioAssets.clear();
    const std::filesystem::path root = mState.assetRoot;
    const std::filesystem::path audioRoot = root / "audio";
    std::error_code error;
    if (!std::filesystem::is_directory(audioRoot, error))
        return;

    for (std::filesystem::recursive_directory_iterator it(audioRoot, error), end;
         it != end && !error; it.increment(error)) {
        if (!it->is_regular_file(error))
            continue;
        std::string extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (extension != ".wav" && extension != ".ogg" &&
            extension != ".flac" && extension != ".mp3")
            continue;
        const std::filesystem::path relative =
            std::filesystem::relative(it->path(), root, error);
        if (!error)
            mAudioAssets.push_back(relative.generic_string());
        error.clear();
    }
    std::sort(mAudioAssets.begin(), mAudioAssets.end());
    refreshAudioCues();
}

// The cue ids in audio.toml, for the actor sound table.
//
// Scanned for `id = "..."` inside `[[cue]]` rather than parsed through the
// catalogue, for the reason GameVocabulary states about enemies: the editor
// needs the list of names, and a scene editor that cannot start because a music
// stem is malformed is one that took on a dependency it did not need.
void EditorApp::refreshAudioCues()
{
    mAudioCues.clear();
    const std::filesystem::path path =
        eng::assets::resolve("config/audio.toml");
    if (path.empty())
        return;
    std::ifstream in(path);
    if (!in)
        return;

    bool inCue = false;
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t begin = line.find_first_not_of(" \t");
        if (begin == std::string::npos)
            continue;
        const std::string_view trimmed(line.c_str() + begin);
        if (trimmed.front() == '[') {
            inCue = trimmed.rfind("[[cue]]", 0) == 0;
            continue;
        }
        if (!inCue || trimmed.rfind("id", 0) != 0)
            continue;
        const std::size_t open = line.find('"');
        const std::size_t close = line.find('"', open + 1);
        if (open == std::string::npos || close == std::string::npos)
            continue;
        std::string id = line.substr(open + 1, close - open - 1);
        // Music stems and stingers are section content, not actions an actor
        // performs; offering them in the sound table would be offering a cue
        // that plays a whole score under a footstep.
        if (!id.empty() && id.rfind("music.", 0) != 0)
            mAudioCues.push_back(std::move(id));
    }
    std::sort(mAudioCues.begin(), mAudioCues.end());
    mAudioCues.erase(std::unique(mAudioCues.begin(), mAudioCues.end()),
                     mAudioCues.end());
    eng::log::info("Editor: %zu audio cues", mAudioCues.size());
}

void EditorApp::previewAudio(const AuthorId& id)
{
    stopAudioPreview();
    if (!mAudio || !mAudio->ready()) {
        mStatus = "audio preview backend is unavailable";
        return;
    }
    const Entity* entity = mState.document.find(id);
    if (!entity || !entity->audio || entity->audio->source.empty())
        return;

    const AudioEmitterAuthor& authored = *entity->audio;
    std::filesystem::path source = authored.source;
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error))
        source = eng::assets::resolve(authored.source);
    if (source.empty()) {
        mStatus = "audio clip does not resolve: " + authored.source;
        return;
    }

    eng::PlaybackSettings settings;
    settings.bus = authored.bus > static_cast<int>(eng::AudioBus::Master) &&
                           authored.bus < static_cast<int>(eng::AudioBus::Count)
                       ? static_cast<eng::AudioBus>(authored.bus)
                       : eng::AudioBus::Sfx;
    settings.gainDb = authored.gainDb;
    settings.pitch = authored.pitch;
    settings.loop = authored.loop;
    settings.streaming = authored.streaming;
    settings.spatialized = authored.spatialized;
    const WorldTransform world = mState.document.worldTransform(id);
    settings.position =
        world.position + world.orientation * (world.scale * authored.offset);
    settings.minDistance = authored.minDistance;
    settings.maxDistance = authored.maxDistance;
    settings.rolloff = authored.rolloff;
    settings.dopplerFactor = authored.dopplerFactor;
    settings.stealable = authored.stealable;
    if (authored.priority <= static_cast<int>(eng::AudioPriority::Background))
        settings.priority = eng::AudioPriority::Background;
    else if (authored.priority <= static_cast<int>(eng::AudioPriority::Low))
        settings.priority = eng::AudioPriority::Low;
    else if (authored.priority <= static_cast<int>(eng::AudioPriority::Normal))
        settings.priority = eng::AudioPriority::Normal;
    else if (authored.priority <=
             static_cast<int>(eng::AudioPriority::Important))
        settings.priority = eng::AudioPriority::Important;
    else
        settings.priority = eng::AudioPriority::Critical;

    mAudioPreview = mAudio->play(source.string(), settings);
    if (!mAudioPreview) {
        mStatus = "could not preview " + authored.source;
        return;
    }
    mAudioPreviewEntity = id;
    mAudioPreviewSource = authored.source;
    mStatus = "previewing " + authored.source;
}

void EditorApp::stopAudioPreview()
{
    if (mAudioPreview)
        mAudioPreview->stop(eng::StopMode::Immediate);
    mAudioPreview.reset();
    mAudioPreviewEntity.clear();
    mAudioPreviewSource.clear();
}

bool EditorApp::loadScene(const std::string& path)
{
    eng::telemetry::event("scene", "load " + path);
    finishInspectorEdit();
    stopAudioPreview();
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
    mState.hidden.clear();
    mState.locked.clear();
    mSelectionAnchor.clear();
    mOutlinerReveal.clear();
    mOutlinerRows.ids.clear();
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

// --- open scenes -------------------------------------------------------------
//
// mState is a *mirror* of the active tab, not a second copy of the truth. These
// two functions are the only place that relationship is maintained, which is
// why no panel below here knows tabs exist: every one of them still reads
// mState.document and mState.selection exactly as it did.

void EditorApp::captureActiveTab()
{
    SceneTab& tab = mTabs.current();
    tab.path = mState.scenePath;
    tab.document = mState.document;
    tab.dirty = mState.dirty;
    tab.commands = mCommands;
    tab.selection = mState.selection;
    tab.camera = mState.camera;
    tab.hidden = mState.hidden;
    tab.locked = mState.locked;
    tab.layerSession = mState.layerSession;
    tab.cookStatus = mCookStatus;
}

void EditorApp::activateTab(std::size_t index)
{
    if (index >= mTabs.size() || index == mTabs.active())
        return;

    // Anything mid-gesture belongs to the document being left. Closing them
    // here rather than letting them land on the new one is the difference
    // between a tab switch and a corrupted scene.
    finishInspectorEdit();
    finishStroke();
    stopAudioPreview();
    if (mState.isolating())
        leaveIsolation();

    captureActiveTab();
    mTabs.activate(index);
    const SceneTab& tab = mTabs.current();

    mState.scenePath = tab.path;
    mState.document = tab.document;
    mState.dirty = tab.dirty;
    mCommands = tab.commands;
    mState.selection = tab.selection;
    mState.camera = tab.camera;
    mState.hidden = tab.hidden;
    mState.locked = tab.locked;
    mState.layerSession = tab.layerSession;
    mCookStatus = tab.cookStatus;

    // Every cache keyed on a document revision has to be dropped, not compared:
    // two documents keep their own revision counters, so the new one can
    // legitimately arrive holding the number the old one last had.
    mIssuesRevision = ~uint64_t(0);
    mContractRevision = ~uint64_t(0);
    mOutlinerRevision = ~uint64_t(0);
    mGizmoMarksRevision = ~uint64_t(0);
    mLayerRowsRevision = ~uint64_t(0);
    mSelectionAnchor.clear();
    mOutlinerReveal.clear();
    mOutlinerRows.ids.clear();
    mPickCycle.reset();
    if (mPreview)
        mPreview->invalidate();

    // The contract decides which editor this scene wants. Opening a 2D page
    // into the 3D level view showed it edge-on, and the fix was something you
    // had to already know about.
    setMainScreen(mainScreenForKind(contract().kind));
    mTabBarFollowsActive = true;
    mStatus = "switched to " + sceneTabName(tab);
}

void EditorApp::openSceneInTab(const std::string& path)
{
    if (path.empty())
        return;

    const std::size_t existing = mTabs.indexOfPath(path);
    if (existing < mTabs.size()) {
        activateTab(existing);
        mStatus = sceneTabName(mTabs.current()) + " is already open";
        return;
    }

    // An untouched blank tab is scaffolding, not work: opening into it is what
    // an author means, and leaving it behind would put an empty scene beside
    // every file they open.
    const bool reuseBlank = mTabs.current().path.empty() && !mState.dirty &&
                            mState.document.entities.empty();
    if (!reuseBlank) {
        captureActiveTab();
        mTabs.open(SceneTab{});
        mCommands.clear();
    }

    if (!loadScene(path)) {
        // The tab that was just made now holds nothing. Close it rather than
        // leaving an empty one named after a file that would not open.
        if (!reuseBlank)
            closeTab(mTabs.active());
        mRecent.remove(path);
        mRecent.save(mRecentFile);
        return;
    }

    mCommands.clear();
    mCookStatus = "not cooked";
    mAutosaveOffered = false;
    mContractRevision = ~uint64_t(0);
    // A recovered backup is not a scene you meant to open: it is one you meant
    // to *rescue*, and it stops existing the moment it is saved over the real
    // file.
    if (!isAutosavePath(path)) {
        mRecent.touch(path);
        mRecent.save(mRecentFile);
    }
    if (autosaveIsStale(path, mState.assetRoot + "/scenes"))
        mStatus += "  |  a newer autosave exists -- Scene > Recover autosave";

    captureActiveTab();
    setMainScreen(mainScreenForKind(contract().kind));
    mTabBarFollowsActive = true;
}

void EditorApp::newSceneInTab(SceneTemplate which, SceneKind kind)
{
    const bool reuseBlank = mTabs.current().path.empty() && !mState.dirty &&
                            mState.document.entities.empty();
    if (!reuseBlank) {
        captureActiveTab();
        mTabs.open(SceneTab{});
        mCommands.clear();
    }

    newScene(which);

    // The kind is applied after the template, not instead of it: a Room
    // template already carries a spawn, and setSceneView only decides what the
    // scene is *looked through*. Empty means "leave whatever the template
    // brought", which is what a game-driven level wants.
    if (kind != SceneKind::Empty && kind != SceneKind::GameDriven) {
        const AuthorId id = setSceneView(mState.document, kind);
        if (!id.empty())
            mState.document.touch();
    }
    mCommands.clear(); // the template plus its view is the starting state
    mContractRevision = ~uint64_t(0);
    captureActiveTab();
    setMainScreen(mainScreenForKind(contract().kind));
    mTabBarFollowsActive = true;
}

void EditorApp::requestCloseTab(std::size_t index)
{
    if (index >= mTabs.size())
        return;
    // The active tab's unsaved state lives in mState, not yet in the tab.
    const bool dirty =
        index == mTabs.active() ? mState.dirty : mTabs.at(index).dirty;
    if (!dirty) {
        closeTab(index);
        return;
    }
    mPendingCloseTab = index;
    // Closing a tab that is not the active one has to become the active one
    // first: the save/discard prompt saves *the open document*, and offering to
    // save one scene while showing another is how the wrong file gets written.
    activateTab(index);
    mPendingCloseTab = mTabs.active();
    requestDiscard(Discard::CloseTab);
}

void EditorApp::closeTab(std::size_t index)
{
    if (index >= mTabs.size())
        return;
    const bool active = index == mTabs.active();
    if (!active)
        captureActiveTab();

    const std::string name = sceneTabName(mTabs.at(index));
    const bool blanked = mTabs.close(index);

    // Either way the active tab now holds a different document from the one
    // mState mirrors, so the mirror is rebuilt from scratch. The guard in
    // activateTab (index == active) would refuse, which is why this restores by
    // hand instead of calling it.
    finishInspectorEdit();
    finishStroke();
    if (mState.isolating())
        leaveIsolation();
    const SceneTab& tab = mTabs.current();
    mState.scenePath = tab.path;
    mState.document = tab.document;
    mState.dirty = tab.dirty;
    mCommands = tab.commands;
    mState.selection = tab.selection;
    mState.camera = tab.camera;
    mState.hidden = tab.hidden;
    mState.locked = tab.locked;
    mState.layerSession = tab.layerSession;
    mCookStatus = tab.cookStatus;
    mState.document.touch();

    mIssuesRevision = ~uint64_t(0);
    mContractRevision = ~uint64_t(0);
    mOutlinerRevision = ~uint64_t(0);
    mGizmoMarksRevision = ~uint64_t(0);
    mLayerRowsRevision = ~uint64_t(0);
    mSelectionAnchor.clear();
    mOutlinerReveal.clear();
    mOutlinerRows.ids.clear();
    mPickCycle.reset();
    if (mPreview)
        mPreview->invalidate();
    setMainScreen(mainScreenForKind(contract().kind));
    mTabBarFollowsActive = true;
    mStatus = "closed " + name + (blanked ? " -- back to an empty scene" : "");
}

void EditorApp::setMainScreen(MainScreen screen)
{
    mScreen = screen;
    switch (screen) {
    case MainScreen::Scene3D:
        if (materialMode())
            setMode(ViewportMode::Level);
        requestPanelFocus("viewport");
        break;
    case MainScreen::Screen2D:
        if (materialMode())
            setMode(ViewportMode::Level);
        requestPanelFocus("ui");
        break;
    case MainScreen::Material:
        setMode(ViewportMode::Material);
        mAssetBrowserModeRequest = 1; // the materials tab, beside the stage
        requestPanelFocus("viewport");
        break;
    }
}

const ContractReport& EditorApp::contract()
{
    if (mContractRevision != mState.document.revision) {
        mContractRevision = mState.document.revision;
        mContract = sceneContract(mState.document);
    }
    return mContract;
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

// --- isolation -------------------------------------------------------------

void EditorApp::enterIsolation(const AuthorId& id)
{
    if (!mState.document.find(id))
        return;
    if (mState.isolating() && mState.isolation.root == id)
        return; // already here; a second double-click is not a toggle

    // You cannot edit an object on the material stage, so that mode yields.
    if (materialMode())
        setMode(ViewportMode::Level);

    // Saved on the FIRST entry only. Isolating a child from inside an object
    // -- which is how you work down a chandelier to one candle -- must still
    // return to the level on the way out, not to the parent's framing.
    if (!mState.isolating()) {
        mState.isolation.cameraBefore = mState.camera;
        mState.isolation.gridLevelBefore = mState.gridState.level;
        mState.isolation.gridCutBefore = mState.gridState.cutAboveLevel;
    }

    eng::telemetry::event("editor", "isolate " + id);
    mState.mode = ViewportMode::Isolate;
    mState.isolation.root = id;
    mState.isolation.membersRevision = ~uint64_t(0);
    refreshIsolation();

    // The grid becomes the ground the object stands on. This is the whole of
    // the "superflat sandbox": the level is hidden, and the one plane left is
    // at the object's own height rather than at whatever the level's work
    // plane happened to be.
    mState.gridState.level = mState.document.worldTransform(id).position.y;
    mState.gridState.cutAboveLevel = false;

    mState.select(id);
    mOutlinerReveal = id;

    glm::vec3 min, max;
    if (boundsOf({id}, min, max))
        frameIsolated(min, max);
}

void EditorApp::leaveIsolation()
{
    if (!mState.isolating())
        return;
    const AuthorId was = mState.isolation.root;
    mState.mode = ViewportMode::Level;
    mState.isolation.root.clear();
    mState.isolation.members.clear();
    mState.isolation.membersRevision = ~uint64_t(0);
    mState.camera = mState.isolation.cameraBefore;
    mState.gridState.level = mState.isolation.gridLevelBefore;
    mState.gridState.cutAboveLevel = mState.isolation.gridCutBefore;
    // Reveal what was being edited, so the level comes back with the object
    // still under the cursor rather than somewhere in three hundred rows.
    if (!was.empty() && mState.document.find(was))
        mOutlinerReveal = was;
}

void EditorApp::refreshIsolation()
{
    if (!mState.isolating())
        return;
    // An undo can delete the entity being edited. A mode pinned to an id that
    // no longer exists is an empty viewport with no way out of it, so leaving
    // is not optional -- and it has to be checked against the document rather
    // than trusted from entry.
    if (!mState.document.find(mState.isolation.root)) {
        leaveIsolation();
        return;
    }
    if (mState.isolation.membersRevision == mState.document.revision)
        return;
    mState.isolation.membersRevision = mState.document.revision;
    mState.isolation.members.assign(1, mState.isolation.root);
    for (const AuthorId& id :
         mState.document.descendantsOf(mState.isolation.root))
        mState.isolation.members.push_back(id);
}

AuthorId EditorApp::parentForNewEntity() const
{
    return mState.isolating() ? mState.isolation.root : AuthorId{};
}

// The layer a newly created entity lands in: whichever row the Layers panel has
// active. The same idea as the active layer in an image editor, and the reason
// the panel makes a row active on a plain click.
//
// Only stamped when the entity does not already name one -- a duplicate, a
// paste or a merged import brought its own layer with it, and having "new
// things go in the active layer" quietly re-file a paste would be a surprise
// nobody could undo selectively.
void EditorApp::stampLayer(Entity& entity) const
{
    if (entity.layer.empty())
        entity.layer = mState.layerSession.active;
}

// Turns a compound kit piece's baked attachments into real child entities.
//
// kit.prop_boss_placeholder ships with its sword declared in kit.toml, and the
// cook emits it as an ECS child at build time. That renders correctly and is
// the right default -- a scene should not carry an entity per torch bracket --
// but it means the sword is not in the document: it cannot be selected, moved,
// re-materialled or given a component, and isolating the boss reports "0 parts"
// while a sword is plainly visible in its hand.
//
// Unpacking writes those parts out where the author can reach them, and marks
// the root so the cook stops generating its own copies. Recursive, because an
// attachment may declare attachments of its own.
//
// One command, so a single undo puts it back.
void EditorApp::unpackAttachments(const AuthorId& id)
{
    const Entity* root = mState.document.find(id);
    if (!root || root->unpackedAttachments)
        return;
    const KitPiece* piece = mState.catalog.find(root->prefab);
    if (!piece || piece->attachments.empty())
        return;

    // Ids are allocated against a copy that already holds the ones this loop
    // has handed out, so two attachments of the same prefab cannot collide.
    Doc scratch = mState.document;
    std::vector<Command> parts;
    std::vector<AuthorId> created;
    for (const Entity& child :
         buildAttachmentEntities(mState.catalog, scratch, *root)) {
        created.push_back(child.id);
        parts.push_back(makeCreateEntity(child));
    }
    if (parts.empty())
        return;

    Entity after = *root;
    after.unpackedAttachments = true;
    parts.push_back(makeEditEntity("unpack attachments", id, *root, after));
    runCommand(makeComposite("unpack attachments", std::move(parts)));

    mPreview->invalidate();
    mStatus = "unpacked " + std::to_string(created.size()) +
              (created.size() == 1 ? " part" : " parts") + " of " +
              (root->name.empty() ? id : root->name);
    if (!created.empty())
        selectAndReveal(created.front(), false);
}

// True when there is anything to unpack: the entity is a compound kit piece
// and has not been unpacked already.
bool EditorApp::canUnpackAttachments(const AuthorId& id) const
{
    const Entity* entity = mState.document.find(id);
    return entity && hasPackedAttachments(mState.catalog, *entity);
}

// Puts a freshly placed entity inside the object being edited.
//
// Without this the mode is read-only in practice: you isolate an object to add
// a part to it, and the part appears as a sibling out in the level -- invisible,
// because isolation is hiding everything that is not in the subtree.
//
// The transform has to be converted, not just reparented. Placement computes a
// world position (that is where the cursor is); once the entity hangs under a
// parent the file stores a local one, and localFromWorld is what keeps it where
// it was dropped.
void EditorApp::adoptIntoIsolation(Entity& entity) const
{
    const AuthorId parent = parentForNewEntity();
    // An entity that already named a parent brought its own frame with it --
    // a duplicate, a paste, a room piece -- and must not be re-based.
    if (parent.empty() || !entity.parent.empty() || entity.id == parent)
        return;
    game::content::WorldTransform world;
    world.position = entity.transform.position;
    world.orientation =
        game::content::authorOrientation(entity.transform.rotationDegrees);
    world.scale = entity.transform.scale;
    entity.parent = parent;
    entity.transform = game::content::localFromWorld(
        mState.document.worldTransform(parent), world);
}

// A gentler pitch than frameSelectionOrAll's. That one looks steeply down
// because a dungeon is a closed box seen from above; an isolated object is a
// thing on a table, and a three-quarter view is how you judge one.
void EditorApp::frameIsolated(const glm::vec3& min, const glm::vec3& max)
{
    const glm::vec3 centre = (min + max) * 0.5f;
    const float radius = glm::max(glm::length(max - min) * 0.5f, 0.5f);
    const float distance =
        radius / std::tan(glm::radians(EditorCamera::kEditorFovDeg * 0.5f)) *
        1.9f;
    const float pitch = -0.42f;
    const float yaw = 0.6f;
    const glm::vec3 back{std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                         std::cos(yaw) * std::cos(pitch)};
    mState.camera.leaveWalk();
    mState.camera.setYawPitch(yaw, pitch);
    mState.camera.setFlyPosition(centre + back * distance);
    mState.camera.frame(centre, distance);
}

void EditorApp::runCommand(Command command)
{
    finishInspectorEdit();
    ENG_TELEMETRY("edit", eng::telemetry::Level::Info, "%s",
                  command.label.empty() ? "(unlabelled)" : command.label.c_str());
    mCommands.run(mState.document, std::move(command));
    mState.dirty = !mCommands.savedStateReached();
    // Any edit invalidates the cooked map; saying so beats letting someone
    // playtest a map that predates the change they are looking at.
    mCookStatus = "stale";
}

void EditorApp::requestMaterialPreview(const std::string& material)
{
    if (material.empty() || (mPreviewSubject == PreviewSubject::Material &&
                             mPreviewName == material))
        return;
    if (!mEngine)
        return;
    eng::Renderer& renderer = mEngine->renderer();
    if (!mStage.thumbnailBuilt())
        mStage.buildThumbnail(renderer, 256);
    // Take the swatch back off the particle subject: the effect keeps running
    // otherwise, and the sphere would be composited over a live burst.
    if (mParticleThumbnail.valid()) {
        renderer.despawnParticles(mParticleThumbnail);
        mParticleThumbnail = {};
    }
    if (mParticleThumbnailNode.valid())
        renderer.setNodeVisible(mParticleThumbnailNode, false);
    mParticleThumbnailEffect.clear();
    mStage.setThumbnailVisible(renderer, true);
    mStage.setThumbnailMaterial(renderer, material);
    mPreviewSubject = PreviewSubject::Material;
    mPreviewName = material;
}

void EditorApp::requestEffectPreview(const std::string& effect)
{
    if (effect.empty() ||
        (mPreviewSubject == PreviewSubject::Effect && mPreviewName == effect))
        return;
    if (!mEngine)
        return;
    eng::Renderer& renderer = mEngine->renderer();
    if (!mStage.thumbnailBuilt())
        mStage.buildThumbnail(renderer, 256);
    // The swatch RTT is a generic isolated square despite its legacy name: hide
    // the material sphere and put the effect at the camera's focus, marked
    // thumbnail-only so it cannot leak into the scene viewport.
    mStage.setThumbnailVisible(renderer, false);
    if (!mParticleThumbnailNode.valid())
        mParticleThumbnailNode =
            renderer.createNode(eng::kRootNode, glm::vec3(0.0f, -1000.0f, 0.0f),
                                "particle_catalog_thumbnail");
    renderer.setNodeVisible(mParticleThumbnailNode, true);
    if (mParticleThumbnail.valid())
        renderer.despawnParticles(mParticleThumbnail);
    eng::ParticleSpawnOptions options;
    options.sizeScale = mParticlePreviewScale;
    mParticleThumbnail =
        renderer.spawnParticles(effect, mParticleThumbnailNode, options);
    renderer.setNodeThumbnailOnly(mParticleThumbnailNode, true);
    mParticleThumbnailEffect = effect;
    mParticleThumbnailScale = mParticlePreviewScale;
    mParticleThumbnailRestartAt = std::numeric_limits<double>::max();
    mPreviewSubject = PreviewSubject::Effect;
    mPreviewName = effect;
}

void EditorApp::finishInspectorEdit()
{
    if (!mInspectorEdit.active())
        return;
    const AuthorId id = mInspectorEdit.id();
    // Captured before commit(), which clears the transaction: the fan-out below
    // is a diff against the entity as it was when the interaction opened.
    const std::optional<Entity> before = mInspectorEdit.before();
    std::optional<Command> command =
        mInspectorEdit.commit(mState.document, "edit " + id);
    if (!command)
        return;

    // Multi-object editing, Gregory §15.4.1.6. The Inspector draws the primary,
    // but an edit made with several entities selected reaches all of them --
    // "give these twelve walls that material", "drop these forty pillars by a
    // metre", "make these torches dimmer" is the whole reason to multi-select,
    // and doing it one entity at a time is not a workflow.
    //
    // Field-level: only what the author actually moved fans out, so a mixed
    // selection absorbs the transform edit and quietly ignores the light one.
    // ed::multiedit owns which fields those are and why.
    std::vector<Command> parts;
    parts.push_back(std::move(*command));
    std::vector<std::string> changed;
    if (before && mState.selection.size() > 1) {
        if (const Entity* edited = mState.document.find(id)) {
            changed = multiedit::changedFields(*before, *edited);
            for (const AuthorId& other : mState.selection) {
                if (other == id)
                    continue;
                const Entity* entity = mState.document.find(other);
                if (!entity)
                    continue;
                Entity updated = *entity;
                if (!multiedit::applyDelta(*before, *edited, updated))
                    continue;
                parts.push_back(
                    makeEditEntity("edit " + other, other, *entity, updated));
            }
        }
    }

    // Do not call runCommand(): it intentionally flushes an inspector edit,
    // and this is that flush.
    if (parts.size() == 1) {
        mCommands.run(mState.document, std::move(parts.front()));
    }
    else {
        const std::size_t count = parts.size();
        const std::string what =
            changed.empty() ? std::string("edit")
                            : (changed.size() == 1
                                   ? changed.front()
                                   : changed.front() + " and " +
                                         std::to_string(changed.size() - 1) +
                                         " more");
        mCommands.run(mState.document,
                      makeComposite(what + " on " + std::to_string(count),
                                    std::move(parts)));
        mPreview->invalidate();
        mStatus = "applied " + what + " to " + std::to_string(count) +
                  " selected";
    }
    mState.dirty = !mCommands.savedStateReached();
    mCookStatus = "stale";
}

void EditorApp::applyHistory(bool redo)
{
    finishInspectorEdit();
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
    mState.hidden.clear();
    mState.locked.clear();
    mSelectionAnchor.clear();
    mOutlinerReveal.clear();
    mOutlinerRows.ids.clear();
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

void EditorApp::requestDiscard(Discard what)
{
    if (what == Discard::Quit && mParticlesDirty) {
        ConfirmDialog::open(
            "Discard unsaved particle changes?", mParticles.path(),
            [this] {
                mParticlesDirty = false;
                requestDiscard(Discard::Quit);
            },
            "Discard");
        return;
    }
    mDiscardWhat = what;
    // A clean document has nothing to lose, so the prompt would be pure
    // ceremony. Only unsaved work earns an interruption.
    //
    // Quitting asks about every open tab, not just the visible one: the whole
    // point of tabs is that work can be sitting in a scene nobody is looking
    // at, and a prompt that only knew about the front one would discard it
    // silently.
    const bool dirty =
        what == Discard::Quit ? (mState.dirty || mTabs.anyDirty())
                              : mState.dirty;
    if (!dirty) {
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
        if (!mState.scenePath.empty()) {
            loadScene(mState.scenePath);
            mCommands.clear();
            captureActiveTab();
        }
        break;
    case Discard::CloseTab:
        closeTab(mPendingCloseTab);
        break;
    }
}

// Opening no longer discards anything: the scene gets a tab. This is still the
// one path everything else (recent list, palette, console, the open dialog)
// goes through, so "already open" is decided in one place.
void EditorApp::requestOpen(const std::string& path)
{
    if (path.empty())
        return;
    mOpenSceneOpen = false;
    openSceneInTab(path);
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
                                                         : "Closing this scene";
    ImGui::Text("%s will discard unsaved changes to", verb);
    ImGui::TextUnformatted(mState.scenePath.empty() ? "this untitled scene."
                                                    : mState.scenePath.c_str());
    ImGui::Separator();

    // Save is default and receives initial navigation focus. Enter must activate
    // the focused button, not bypass focus and save while Cancel is selected.
    if (ImGui::Button("Save and continue")) {
        // An untitled scene has nowhere to save to, so the discard waits while
        // Save As collects a path rather than silently failing.
        if (mState.scenePath.empty()) {
            mDiscardOpen = false;
            mContinueDiscardAfterSave = true;
            mSaveAsOpen = true;
            ImGui::CloseCurrentPopup();
        }
        else if (saveScene()) {
            ImGui::CloseCurrentPopup();
            performDiscard();
        }
    }
    if (ImGui::IsWindowAppearing())
        ImGui::SetItemDefaultFocus();
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
    finishInspectorEdit();
    if (mState.scenePath.empty()) {
        mSaveAsOpen = true;
        std::snprintf(mSaveAsPath, sizeof(mSaveAsPath), "%s",
                      (mState.assetRoot + "/scenes/untitled.scn").c_str());
        mStatus = "choose a name for the scene";
        return false;
    }
    return saveSceneTo(mState.scenePath);
}

bool EditorApp::saveSceneTo(const std::string& requestedPath)
{
    finishInspectorEdit();
    if (requestedPath.empty()) {
        mStatus = "a scene path is required";
        return false;
    }
    std::filesystem::path path(requestedPath);
    if (!path.has_extension())
        path.replace_extension(".scn");
    if (path.extension() != ".scn") {
        mStatus = "scene files use the .scn extension";
        return false;
    }
    std::string error;
    if (!writeSceneSource(path.string(), mState.document, error)) {
        mStatus = error;
        return false;
    }
    mState.scenePath = path.string();
    mCommands.markSaved();
    mState.dirty = false;
    // The tab is the identity of an open scene, so a Save As has to reach it:
    // otherwise the tab keeps its "[unsaved]" label and, worse, reopening the
    // file it was just written to would not recognise it as already open.
    captureActiveTab();
    mStatus =
        "saved " + std::filesystem::path(mState.scenePath).filename().string();
    return true;
}

// The cook the CLI does, called in-process. Same function, so the bytes match
// what CI produces -- the cook_parity test exists to keep that true.
bool EditorApp::cookScene(std::string& mapPath)
{
    finishInspectorEdit();
    const std::vector<Issue> issues =
        validate(mState.document, mState.catalog, mState.assetRoot);
    if (blocksCook(issues)) {
        mStatus = "cook refused: fix the blocking issues first";
        mCookStatus = "blocked";
        return false;
    }
    // Beside the .scn normally; inside the project's work directory when one is
    // open, because a cooked map is a build product and a build product in
    // somebody's source tree is one they have to gitignore.
    mapPath = mProject.cookTarget(mState.scenePath, mState.assetRoot);

    std::string error;
    if (!cookToMap(mState.document, mState.catalog, mapPath, error,
                   mState.assetRoot)) {
        mStatus = error;
        mCookStatus = "failed";
        return false;
    }
    mCookStatus = "fresh";
    mStatus = "cooked " + std::filesystem::path(mapPath).filename().string();
    return true;
}

// --- projects ---------------------------------------------------------------
//
// Opening a project repoints the editor's content root at somebody else's
// tree. Everything downstream -- where a new scene saves, what validate()
// checks a mesh against, where a cook lands -- already reads that one string,
// so this is deliberately the whole of the change rather than a parallel set
// of project-aware paths.

void EditorApp::openProject(const std::string& dir)
{
    if (!mProject.open(dir)) {
        mStatus = "not a project: " + dir;
        return;
    }
    mProject.saveRecents(mProjectRecentsFile);
    mState.assetRoot = mProject.contentRoot(mState.assetRoot);
    mStatus = "opened project " + mProject.project().name;
    // Its main scene, because that is what a project says it is about. A
    // project whose main scene will not open still counts as open -- the
    // author is most likely on their way to fixing exactly that.
    openSceneInTab(mProject.mainScenePath());
}

void EditorApp::newProject()
{
    const std::string dir = mProjectPath;
    if (dir.empty()) {
        mStatus = "give the project a directory";
        return;
    }
    if (!mProject.create(dir, mProjectName)) {
        mStatus = "could not create a project at " + dir;
        return;
    }
    mProject.saveRecents(mProjectRecentsFile);
    mState.assetRoot = mProject.contentRoot(mState.assetRoot);
    mNewProjectOpen = false;
    mStatus = "created project " + mProject.project().name;
    openSceneInTab(mProject.mainScenePath());
}

// Export, from the editor. The same function raven_export calls, so a build
// made here and one made on a build machine are the same build.
//
// Into `<project>/../<name>-build`, beside the project rather than inside it:
// a build directory inside the source is one somebody has to gitignore, and the
// exporter refuses to write into a directory that already has things in it.
void EditorApp::exportProjectBuild()
{
    if (!mProject.isOpen())
        return;

    ExportOptions options;
    options.projectDir = mProject.project().dir.string();
    options.outDir = (mProject.project().dir.parent_path() /
                      (mProject.project().dir.filename().string() + "-build"))
                         .string();
    options.playerPath = siblingExecutable(mExecutablePath, "raven_player");
    options.overwrite = true; // re-exporting over the last build is the norm

    mStatus = "exporting...";
    const ExportReport report = exportProject(options);
    if (!report.ok) {
        mStatus = "export failed: " + report.error;
        return;
    }
    char summary[512];
    std::snprintf(summary, sizeof(summary),
                  "exported %zu scene(s) to %s (%.1f MB)%s",
                  report.cookedScenes.size(), report.executable.c_str(),
                  double(report.bytesCopied) / (1024.0 * 1024.0),
                  report.skippedScenes.empty() ? ""
                                               : " -- SOME SCENES SKIPPED");
    mStatus = summary;
    for (const std::string& skipped : report.skippedScenes)
        eng::log::error("Export: skipped %s", skipped.c_str());
}

void EditorApp::closeProject()
{
    mProject.close();
    // Back to the tree this editor ships beside, which is what "no project" has
    // always meant here.
    mState.assetRoot = eng::assets::packDir("content").string();
    mStatus = "closed the project";
}

void EditorApp::runPlaytest()
{
    if (mPlaytest.running()) {
        stopGame(mPlaytest);
        mStatus = "playtest stopped";
        return;
    }
    // The contract's one hard rule: a scene with no view and no player spawn
    // loads, cooks, plays and shows nothing. Launching it produced a black
    // window and no explanation, which is the failure docs/scenes.md was
    // written to make loud -- so this is where it gets made loud.
    if (!contract().playable) {
        mStatus = "this scene has nothing to look through -- see the Contract "
                  "panel";
        requestPanelFocus("contract");
        return;
    }
    // F5 means "play what is on screen". The particle panel edits the live
    // library, so save it alongside the scene before launching the child
    // process; otherwise Preview can show a tuned or renamed effect that the
    // playtest cannot resolve from its older on-disk TOML.
    if (mParticlesDirty) {
        if (!mParticles.save(mParticles.path())) {
            mStatus = "playtest refused: could not save " + mParticles.path();
            return;
        }
        mParticlesDirty = false;
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

    // Which runtime plays it. A project plays on raven_player, which links no
    // game code at all -- that is the whole point of a project -- and the
    // content tree this editor ships beside plays on the game, as it always
    // has. The argument differs to match: the player is handed the project
    // directory so it mounts the project's content, and told which map to open
    // through the environment, the same way every other playtest option is
    // passed.
    const bool project = mProject.isOpen();
    const std::string exe =
        siblingExecutable(mExecutablePath, project ? "raven_player" : "game");
    const std::string target =
        project ? mProject.project().dir.string() : mapPath;
    // Under artifacts/, with every other generated file, rather than dropped in
    // the project root. Asked for by name -- this used to climb "/../.." out of
    // an asset path, which only worked because of where the tree happened to
    // sit. The directory is created because a playtest may be the first thing
    // a fresh clone does.
    const std::filesystem::path logDir = eng::assets::project() / "artifacts";
    std::error_code logDirEc;
    std::filesystem::create_directories(logDir, logDirEc);
    const std::string log = (logDir / "playtest.log").string();
    // Remembered so the Scripts panel knows what to read, and cleared of the
    // previous run's errors so the panel cannot show stale ones as current.
    mPlaytestLogPath = log;
    mScriptIssues.clear();
    mScriptIssuesFromCurrentRun = true;

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
    // F5 means "play what is on screen", which for a project is the scene in
    // the active tab rather than whatever the project calls its main.
    if (project)
        options.playMap = mapPath;
    mPlaytest = launchGame(exe, target, log, playtestEnvironment(options));
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
    const auto deleted = [&ids](const AuthorId& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    mState.hidden.erase(
        std::remove_if(mState.hidden.begin(), mState.hidden.end(), deleted),
        mState.hidden.end());
    mState.locked.erase(
        std::remove_if(mState.locked.begin(), mState.locked.end(), deleted),
        mState.locked.end());
    mSelectionAnchor.clear();
    mOutlinerReveal.clear();
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

bool EditorApp::writeAutosave()
{
    const std::string path =
        autosavePath(mState.scenePath, mState.assetRoot + "/scenes");
    if (path.empty())
        return false;
    std::string error;
    if (!writeSceneSource(path, mState.document, error)) {
        // Reported once, in the log rather than the status bar: a backup that
        // fails must not steal the line the author is reading, but a silent one
        // is worse than none.
        eng::log::warn("editor: autosave failed: %s", error.c_str());
        return false;
    }
    eng::log::info("editor: autosaved to %s", path.c_str());
    return true;
}

void EditorApp::tickAutosave(float dt)
{
    // The rule itself is a pure function in EditorSettings, so it is testable
    // without waiting two minutes with an editor open.
    const AutosaveTick tick =
        stepAutosave(mSettings, mAutosaveIn, dt, mState.dirty);
    mAutosaveIn = tick.remaining;
    if (tick.write)
        (void)writeAutosave();
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
    const auto component = [](const char* id) { return findComponentType(id); };

    // Each edit is preceded by a frame that only selects, so the frame the
    // edit lands on has already drawn the gizmo, the inspector section and the
    // outliner row for that entity -- which is the state a hand-driven click
    // is always in, and the one the crash reports came from.
    struct Step {
        const char* entity; // id prefix to select
        const char* action; // "remove:<component>", "detach", "delete"
    };
    static const Step kSteps[] = {
        {"prop_raccoon", "remove:shader"}, {"prop_raccoon", "remove:mesh"},
        {"prop_raccoon", "remove:spin"},   {"camera_main", "detach"},
        {"camera_main", "remove:camera"},  {"prop_base", "delete"},
        {"prop_crystal", "delete"},        {"light_key", "remove:light"},
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
    }
    else if (action == "delete") {
        deleteSelection();
    }
    else if (action.rfind("remove:", 0) == 0) {
        if (const ComponentType* type = component(action.substr(7).c_str()))
            removeComponentFromSelection(*type);
    }
}

void EditorApp::applySettings()
{
    // The editor's own theme, applied here rather than left to the engine
    // default: the game's debug UI and this share one imgui context but not one
    // job, and the shell drawn above is Godot's arrangement wearing Godot's
    // palette. Unknown ids fall back rather than failing, so a settings file
    // naming a theme that has been removed still opens.
    if (!eng::imguitheme::apply(mSettings.theme))
        eng::imguitheme::apply("godot_dark");

    mGameLighting = mSettings.gameLighting;
    mShowEntityGizmos = mSettings.entityMarks;
    mShowGizmoVolumes = mSettings.volumeMarks;
    mShowFrameStats = mSettings.frameStats;
    mShowGrid = mSettings.grid;
    mPlayFromCamera = mSettings.playFromCamera;
    applyUiScale(mSettings.uiScale);

    if (!mEngine)
        return;
    if (!mSettings.viewportPreset.empty()) {
        const int id =
            eng::renderPresetFromName(mSettings.viewportPreset.c_str());
        if (id > 0) {
            mEngine->setRenderPreset(id);
        }
        else {
            // Named a profile that no longer exists: say so and forget it,
            // rather than reporting it again on every launch forever.
            eng::log::warn("editor: unknown render preset '%s' in settings",
                           mSettings.viewportPreset.c_str());
            mSettings.viewportPreset.clear();
        }
    }
    applySceneEnvironment(mEngine->renderer());
}

void EditorApp::applyUiScale(float scale)
{
    scale = std::clamp(scale, EditorSettings::kMinUiScale,
                       EditorSettings::kMaxUiScale);
    if (std::abs(scale - mAppliedUiScale) < 0.001f)
        return;
    ImGui::GetStyle().ScaleAllSizes(scale / mAppliedUiScale);
    ImGui::GetIO().FontGlobalScale = scale;
    mAppliedUiScale = scale;
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
    // The other direction, once a frame: the Window menu, toolbar and
    // render-preset submenu all edit the live state directly, and a preference
    // that only persists when it was changed from the settings panel is a
    // preference the author will find reset tomorrow. One rule instead of a
    // commit call on every widget that touches one of these.
    EditorSettings next = mSettings;
    next.gameLighting = mGameLighting;
    next.entityMarks = mShowEntityGizmos;
    next.volumeMarks = mShowGizmoVolumes;
    next.frameStats = mShowFrameStats;
    next.grid = mShowGrid;
    next.playFromCamera = mPlayFromCamera;
    if (mEngine)
        next.viewportPreset = eng::renderPresetName(mEngine->renderPreset());

    if (next.gameLighting == mSettings.gameLighting &&
        next.entityMarks == mSettings.entityMarks &&
        next.volumeMarks == mSettings.volumeMarks &&
        next.frameStats == mSettings.frameStats &&
        next.grid == mSettings.grid &&
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

    // The wheel zooms an elevation. In perspective it is already the placement
    // tools' (brush scale and rotation), so this is deliberately scoped to the
    // orthographic views, where a dolly would do nothing visible at all.
    if (mViewportHovered && mState.camera.orthographic() &&
        !mState.camera.walking()) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
            mState.camera.zoomOrtho(wheel > 0.0f ? 0.9f : 1.0f / 0.9f);
    }

    if (mFlying) {
        const glm::vec2 delta = input.mouseDelta();
        constexpr float kLookSpeed = 0.0032f;
        // An elevation cannot be looked around -- its whole value is that the
        // axis is fixed -- so the right-drag gesture pans the framed region
        // instead, which is what the same drag does in every 2D view.
        if (mState.camera.orthographic() && !mState.camera.walking()) {
            if (mViewportH > 1.0f)
                mState.camera.panOrtho({delta.x / mViewportH,
                                        delta.y / mViewportH});
            glm::vec3 move{0.0f};
            if (input.isDown("fly_forward")) move.y += 1.0f;
            if (input.isDown("fly_back")) move.y -= 1.0f;
            if (input.isDown("fly_left")) move.x -= 1.0f;
            if (input.isDown("fly_right")) move.x += 1.0f;
            if (move != glm::vec3(0.0f)) {
                // Scaled by the zoom, so a keypress crosses the same fraction
                // of the screen whether the view spans four metres or four
                // hundred.
                const float pace = (input.isDown("fly_fast") ? 1.2f : 0.4f) *
                                   f.dt;
                mState.camera.panOrtho({-move.x * pace, move.y * pace});
            }
            return;
        }
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
}

void EditorApp::handleShortcuts(const eng::FrameContext& f)
{
    eng::Input& input = f.engine.input();
    const ImGuiIO& io = ImGui::GetIO();

    // This runs after ImGui::NewFrame(), unlike the old onFrameBegin path. The
    // current event's modifiers, keyboard ownership and active widget are now
    // authoritative, so a fast Ctrl chord cannot leak its bare letter action.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P))
        openPalette(mPalette);
    // Ctrl+1..3 are the main screens, Ctrl+4.. the docks. Godot's split, and
    // the reason for it is that switching what the centre shows is a different
    // and far more frequent act than bringing a side panel forward.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_1))
        setMainScreen(MainScreen::Scene3D);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_2))
        setMainScreen(MainScreen::Screen2D);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_3))
        setMainScreen(MainScreen::Material);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_4))
        requestPanelFocus("outliner");
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_5))
        requestPanelFocus("catalog");
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_6))
        requestPanelFocus("inspector");
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_7))
        requestPanelFocus("contract");
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_8))
        requestPanelFocus("layers");
    // Scene tabs.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        mNewSceneOpen = true;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_W))
        requestCloseTab(mTabs.active());
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Tab) &&
        mTabs.size() > 1)
        activateTab((mTabs.active() + 1) % mTabs.size());

    // Navigation, Gregory §15.4.1.3. Alt rather than Ctrl for the bookmarks
    // because Ctrl+digit is already panel focus above, and Alt+arrow is the
    // browser gesture the history is modelled on.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_LeftArrow))
        navigateHistory(false);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_RightArrow))
        navigateHistory(true);
    for (int slot = 0; slot < int(nav::kBookmarkSlots); ++slot) {
        // Slot 0 is on the 1 key and slot 9 on the 0 key, which is where every
        // application that numbers ten of something puts them.
        const ImGuiKey digit =
            slot < 9 ? ImGuiKey(int(ImGuiKey_1) + slot) : ImGuiKey_0;
        if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiMod_Ctrl | digit))
            setBookmark(std::size_t(slot));
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | digit))
            jumpToBookmark(std::size_t(slot));
    }

    // WantCaptureKeyboard also means "an ImGui window has navigation focus";
    // in a docked editor that is almost always true, even when no field is
    // consuming keys. Only an active widget or popup should mute editor
    // shortcuts. This preserves typing while making panel focus irrelevant.
    const bool widgetEditing = ImGui::IsAnyItemActive();
    const bool popupOpen = ImGui::IsPopupOpen(
        "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!widgetEditing && !popupOpen && !mFlying) {
        const bool interactionActive =
            mGizmoDragging || mStroke != Stroke::None || mRoomDragging ||
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
        if (!interactionActive && plain && input.wasPressed("focus"))
            frameSelectionOrAll();
        if (!interactionActive && plain && input.wasPressed("tool_select")) {
            mState.tool = Tool::Select;
            mGizmoOperation = -1;
        }
        if (!interactionActive && plain && input.wasPressed("tool_place"))
            mState.tool = Tool::Place;
        if (!interactionActive && plain && input.wasPressed("tool_room"))
            mState.tool = Tool::Room;
        if (!interactionActive && plain && input.wasPressed("grid_coarser"))
            mState.gridState.coarser();
        if (!interactionActive && plain && input.wasPressed("grid_finer"))
            mState.gridState.finer();
        if (!interactionActive && plain && input.wasPressed("brush_rotate_cw"))
            mState.brush.rotate(1);
        if (!interactionActive && plain && input.wasPressed("brush_rotate_ccw"))
            mState.brush.rotate(-1);
        if (!interactionActive && plain && input.wasPressed("brush_bigger"))
            mState.brush.resize(1.1f);
        if (!interactionActive && plain && input.wasPressed("brush_smaller"))
            mState.brush.resize(1.0f / 1.1f);
        if (!interactionActive && plain && input.wasPressed("brush_reset_size"))
            mState.brush.scale = 1.0f;
        if (!interactionActive && plain && input.wasPressed("toggle_snap"))
            mState.gridState.snap = !mState.gridState.snap;
        if (!interactionActive && plain && input.wasPressed("level_up"))
            mState.gridState.level += mState.grid.cell;
        if (!interactionActive && plain && input.wasPressed("level_down"))
            mState.gridState.level -= mState.grid.cell;
        if (!interactionActive && plain && input.wasPressed("level_reset"))
            mState.gridState.level = 0.0f;
        if (!interactionActive && plain && input.wasPressed("gizmo_move")) {
            mState.tool = Tool::Select;
            mGizmoOperation = 0;
        }
        if (!interactionActive && plain && input.wasPressed("gizmo_rotate")) {
            mState.tool = Tool::Select;
            mGizmoOperation = 1;
        }
        if (!interactionActive && plain && input.wasPressed("gizmo_scale")) {
            mState.tool = Tool::Select;
            mGizmoOperation = 2;
        }
        if (!interactionActive && plain && mGizmoOperation >= 0 &&
            input.wasPressed("gizmo_space"))
            mGizmoLocal = !mGizmoLocal;
        if (!interactionActive && plain && input.wasPressed("walk"))
            toggleWalk();
        if (!interactionActive && plain && input.wasPressed("delete"))
            deleteSelection();

        if (!interactionActive && io.KeyCtrl && input.wasPressed("save"))
            saveScene();
        if (!interactionActive && io.KeyCtrl &&
            input.wasPressed("gizmo_scale") && !mState.scenePath.empty())
            requestDiscard(Discard::Reload);
        if (!interactionActive && plain && input.wasPressed("run"))
            runPlaytest();
        if (!interactionActive && plain && input.wasPressed("cook")) {
            std::string mapPath;
            cookScene(mapPath);
        }
        if (!interactionActive && io.KeyCtrl && input.wasPressed("duplicate"))
            duplicateSelection();
        if (!interactionActive && io.KeyCtrl && input.wasPressed("undo"))
            applyHistory(io.KeyShift);
        if (!interactionActive && io.KeyCtrl && input.wasPressed("redo"))
            applyHistory(true);
        if (!interactionActive && io.KeyCtrl && input.wasPressed("open"))
            mOpenSceneOpen = true;
        if (!interactionActive && io.KeyCtrl && input.wasPressed("copy"))
            copySelection(false);
        if (!interactionActive && io.KeyCtrl && input.wasPressed("cut"))
            copySelection(true);
        if (!interactionActive && io.KeyCtrl && input.wasPressed("paste"))
            pasteClipboard();
        if (!interactionActive && plain && input.wasPressed("help"))
            mHelpOpen = !mHelpOpen;
        if (!interactionActive && plain && input.wasPressed("dev_console"))
            mBottom.toggle(BottomTab::Output);
    }

    if (!input.wasPressed("quit"))
        return;
    // Let an active text/numeric field and the top ImGui popup consume Escape;
    // global back must never also clear the selection or close the workspace.
    if (ImGui::IsAnyItemActive() ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId |
                                   ImGuiPopupFlags_AnyPopupLevel))
        return;
    if (mGizmoDragging || mStroke != Stroke::None || mRoomDragging) {
        mStatus = "finish or release the active edit before leaving the tool";
    }
    else if (ConfirmDialog::isOpen()) {
        ConfirmDialog::cancel();
    }
    else if (mPalette.open) {
        mPalette.open = false;
    }
    else if (materialMode()) {
        setMode(ViewportMode::Level);
    }
    else if (mState.isolating()) {
        // Esc walks out one layer at a time, and isolation is the outermost
        // thing it can be in once the popups and the brush are dealt with.
        leaveIsolation();
    }
    else if (!mState.selection.empty()) {
        finishInspectorEdit();
        mState.selection.clear();
    }
    else if (mState.tool != Tool::Select) {
        mState.tool = Tool::Select;
    }
    else {
        requestDiscard(Discard::Quit);
    }
}

// Editor verbs the console can reach. Every one of them is a call the menus
// already make: the console is a second way in, not a second implementation.
void EditorApp::installConsoleCommands()
{
    mConsole.captureEngineLog();
    // The console is a bottom-panel body now, so its own visible flag no longer
    // gates it -- drawBody ignores it and drains the log regardless. Set anyway
    // so anything that still asks (the palette's "Console" verb) reads true.
    mConsole.setVisible(true);
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
            for (const SceneEntry& entry :
                 listScenes(mState.assetRoot + "/scenes"))
                names.push_back(entry.name);
            return names;
        });
}

void EditorApp::onUpdate(const eng::FrameContext& f)
{
    // What the editor currently IS, glanceable on a second monitor instead of
    // occupying panel space in the editor itself.
    if (eng::telemetry::enabled("editor", eng::telemetry::Level::Trace)) {
        eng::telemetry::watch("editor", "scene",
                              mState.scenePath.empty() ? "(unsaved)"
                                                       : mState.scenePath);
        eng::telemetry::watchValue("editor", "entities",
                                   double(mState.document.entities.size()));
        eng::telemetry::watchValue("editor", "selected",
                                   double(mState.selection.size()));
        eng::telemetry::watch("editor", "dirty", mState.dirty ? "yes" : "no");
        eng::telemetry::watch("editor", "mode",
                              mState.mode == ViewportMode::Isolate ? "isolate"
                              : mState.mode == ViewportMode::Material
                                  ? "material"
                                  : "level");
        if (mState.isolating())
            eng::telemetry::watch("editor", "isolated", mState.isolation.root);
        if (const AuthorId* primary = mState.primary())
            eng::telemetry::watch("editor", "primary", *primary);
        const glm::vec3 eye = mState.camera.activeEye();
        eng::telemetry::watchf("editor", "camera", "%.1f %.1f %.1f", eye.x,
                               eye.y, eye.z);
    }
    tickAutosave(f.dt);
    eng::Renderer& renderer = f.engine.renderer();
    // Editing particles.toml in a text editor should land without a restart,
    // the same as editing it through the panel. Re-registration keeps effect
    // ids stable, so live instances survive the reload.
    // Never replace unsaved panel edits with a hot reload. Saving clears the
    // guard; until then the local authoring state is authoritative.
    if (!mParticlesDirty)
        mParticles.reloadIfChanged(renderer);
    // Repro hook: walk the material list the way a user scrubbing it would.
    if (mCycleMaterials && !mMaterialNames.empty() && mStage.thumbnailBuilt()) {
        mCycleIndex = (mCycleIndex + 1) % mMaterialNames.size();
        const std::string& name = mMaterialNames[mCycleIndex];
        mSelectedMaterial = name;
        mStage.setThumbnailMaterial(renderer, name);
        if (materialMode())
            mStage.setMaterial(renderer, name);
    }

    // The swatch turns whether or not the staging mode is open: it is a live
    // preview in the corner of the material list, not a mode.
    if (mThumbAutoSpin && mStage.thumbnailBuilt())
        mStage.spinThumbnail(renderer, mStage.thumbnailSpin() + 0.6f * f.dt);

    if (materialMode()) {
        if (mStageAutoSpin)
            mStage.setSpin(renderer, mStage.spin() + mStageSpinSpeed * f.dt);
    }
    else {
        mPreview->sync(mState.document, mState.catalog, mState.assetRoot);
        // The previewed hands animate on the frame clock; the document has not
        // changed, so this cannot ride the revision-guarded sync above.
        mPreview->tickViewmodel(f.dt);
        // Authored clips, for the same reason and with the same caveat: the
        // document has not changed, the clip is simply playing. Clips only --
        // NOT tickComponentSystems -- so authored entities do not spin and
        // expire under the author's cursor (see PreviewBridge::tickClips).
        mPreview->tickClips(f.dt);
        // The whole scene, every storey, unless the author asks for the cut.
        // The cut is still here because a ceiling becomes a lid over a
        // top-down view, but it is opt-in now: the work plane is a placement
        // and snapping control, and letting it also decide visibility meant a
        // multi-storey scene could never be viewed as a whole.
        mPreview->setCeilingCut(
            renderer, mState.gridState.cutAboveLevel
                          ? mState.gridState.level + mState.grid.cell * 0.5f
                          : std::numeric_limits<float>::infinity());
        mPreview->setHiddenEntities(renderer, mState.hidden);
        // After the document is synced, so a member added this frame is in the
        // set before the visibility pass reads it -- otherwise a part placed
        // inside an object flickers into view a frame late.
        refreshIsolation();
        mPreview->setIsolation(renderer, mState.isolating(),
                               mState.isolation.members);
    }
    // Walk mode hands the renderer the player's eye and the game's field of
    // view, so the viewport shows exactly the frame the player will get.
    renderer.setEditorCameraPose(mState.camera.activeEye(),
                                 mState.camera.activeOrientation(),
                                 mState.camera.activeFovDeg());
    // Zero in perspective and in walk mode, which is the renderer's own
    // "use the perspective path". Pushed every frame beside the pose so
    // leaving an elevation cannot leave the viewport orthographic.
    renderer.setEditorCameraOrtho(mState.camera.activeOrthoHeight());
    if (mAudio) {
        eng::AudioListener listener;
        listener.position = mState.camera.activeEye();
        listener.forward = mState.camera.activeOrientation() *
                           glm::vec3(0.0f, 0.0f, -1.0f);
        listener.up = mState.camera.activeOrientation() *
                      glm::vec3(0.0f, 1.0f, 0.0f);
        mAudio->setListener(listener);

        if (mAudioPreview) {
            const Entity* entity = mState.document.find(mAudioPreviewEntity);
            if (!entity || !entity->audio ||
                entity->audio->source != mAudioPreviewSource) {
                stopAudioPreview();
            } else {
                const WorldTransform world =
                    mState.document.worldTransform(entity->id);
                const glm::vec3 position =
                    world.position + world.orientation *
                                         (world.scale * entity->audio->offset);
                mAudioPreview->setPosition(position);
                mAudioPreview->setGainDb(entity->audio->gainDb);
                mAudioPreview->setPitch(entity->audio->pitch);
            }
        }
        mAudio->update(f.dt);
    }
    if (materialMode())
        renderer.setDebugLines({}); // the checkerboard is the reference here
    else
        updateGridLines(renderer);
    renderer.frameStats(mBatches, mTriangles);

    // While the game is up, re-read its log on a timer. Twice a second is far
    // below anything a person notices and far above what a file read costs.
    if (mPlaytest.running()) {
        mScriptIssuePoll -= f.realDt;
        if (mScriptIssuePoll <= 0.0f) {
            mScriptIssuePoll = 0.5f;
            refreshScriptIssues();
        }
    }

    int exitCode = 0;
    if (mPlaytest.running() && !pollGame(mPlaytest, exitCode)) {
        // Once more after it exits: the errors that killed it are written last.
        refreshScriptIssues();
        mStatus = exitCode == 0 ? "playtest finished"
                                : "playtest exited with code " +
                                      std::to_string(exitCode) +
                                      " -- see artifacts/playtest.log";
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
    if (!mShowGrid) {
        renderer.setDebugLines(lines);
        return;
    }

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
    add("New scene...", "scene", "Ctrl+N", true,
        [this] { mNewSceneOpen = true; });
    add("Close scene", "scene", "Ctrl+W", true,
        [this] { requestCloseTab(mTabs.active()); });
    // Switching between the scenes already open, by name. The palette is the
    // only surface that reaches a background tab without the mouse.
    for (std::size_t i = 0; i < mTabs.size(); ++i) {
        if (i == mTabs.active())
            continue;
        add(
            "Go to " + sceneTabName(mTabs.at(i)), "scene", "", true,
            [this, i] { activateTab(i); }, "open scene");
    }
    add("Open scene...", "scene", "Ctrl+O", true,
        [this] { mOpenSceneOpen = true; });
    for (const std::string& path : mRecent.paths()) {
        add(
            "Open " + std::filesystem::path(path).filename().string(), "scene",
            "", true, [this, path] { requestOpen(path); }, "recent");
    }
    add(
        "Save scene", "scene", "Ctrl+S", true, [this] { saveScene(); },
        mState.scenePath);
    add("Save scene as...", "scene", "", true, [this] {
        mSaveAsOpen = true;
        std::snprintf(mSaveAsPath, sizeof(mSaveAsPath), "%s",
                      mState.scenePath.empty()
                          ? (mState.assetRoot + "/scenes/untitled.scn").c_str()
                          : mState.scenePath.c_str());
    });
    add("Reload scene from disk", "scene", "Ctrl+R", !mState.scenePath.empty(),
        [this] { requestDiscard(Discard::Reload); });
    add(
        "Recover autosave", "scene", "",
        autosaveIsStale(mState.scenePath, mState.assetRoot + "/scenes"),
        [this] { recoverAutosave(); },
        "the backup written while the last session was unsaved");
    add("Cook scene to a runtime map", "scene", "F6", true, [this] {
        std::string mapPath;
        cookScene(mapPath);
    });
    add(
        mPlaytest.running() ? "Stop playtest" : "Run playtest", "scene", "F5",
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
    add("Redo " + mCommands.redoLabel(), "edit", "Ctrl+Y / Ctrl+Shift+Z",
        mCommands.canRedo(), [this] { applyHistory(true); });
    const bool hasSelection = !mState.selection.empty();
    add("Copy selection", "edit", "Ctrl+C", hasSelection,
        [this] { copySelection(false); });
    add("Cut selection", "edit", "Ctrl+X", hasSelection,
        [this] { copySelection(true); });
    add(
        "Paste", "edit", "Ctrl+V", !mClipboard.empty(),
        [this] { pasteClipboard(); },
        mClipboard.empty() ? ""
                           : std::to_string(mClipboard.size()) + " copied");
    add("Duplicate selection", "edit", "Ctrl+D", hasSelection,
        [this] { duplicateSelection(); });
    add("Delete selection", "edit", "Del", hasSelection,
        [this] { deleteSelection(); });
    add("Select nothing", "edit", "Esc", hasSelection,
        [this] { mState.selection.clear(); });
    add(
        "Parent selection to first selected", "edit", "",
        mState.selection.size() > 1, [this] { parentSelectionToPrimary(); },
        "makes a composed object that moves as one");
    add("Detach selection from parent", "edit", "", hasSelection,
        [this] { detachSelection(); });

    // --- tools and view ------------------------------------------------------
    add("Select tool", "tool", "Q", true, [this] {
        mState.tool = Tool::Select;
        mGizmoOperation = -1;
    });
    add("Place tool", "tool", "P", true, [this] { mState.tool = Tool::Place; });
    add("Room tool", "tool", "B", true, [this] { mState.tool = Tool::Room; });
    add("Move selection", "tool", "W", true, [this] {
        mState.tool = Tool::Select;
        mGizmoOperation = 0;
    });
    add("Rotate selection", "tool", "E", true, [this] {
        mState.tool = Tool::Select;
        mGizmoOperation = 1;
    });
    add("Scale selection", "tool", "R", true, [this] {
        mState.tool = Tool::Select;
        mGizmoOperation = 2;
    });
    add("Focus selection", "view", "F", true,
        [this] { frameSelectionOrAll(); });
    add("Toggle snap to grid", "view", "G", true,
        [this] { mState.gridState.snap = !mState.gridState.snap; });
    add("Raise work plane", "view", "PageUp", true,
        [this] { mState.gridState.level += mState.grid.cell; });
    add("Lower work plane", "view", "PageDown", true,
        [this] { mState.gridState.level -= mState.grid.cell; });
    add("Reset work plane to zero", "view", "Home", true,
        [this] { mState.gridState.level = 0.0f; });
    add(mGizmoLocal ? "Gizmo axes: world" : "Gizmo axes: local", "view", "X",
        true, [this] { mGizmoLocal = !mGizmoLocal; });
    add(mState.camera.walking() ? "Leave walk camera" : "Walk the level",
        "view", "V", true, [this] { toggleWalk(); });
    add(mGameLighting ? "Editor lighting" : "Game lighting", "view", "", true,
        [this] {
            mGameLighting = !mGameLighting;
            if (mEngine)
                applySceneEnvironment(mEngine->renderer());
        });
    add(materialMode() ? "Leave material stage" : "Material stage", "view", "",
        true, [this] {
            setMode(materialMode() ? ViewportMode::Level
                                   : ViewportMode::Material);
        });
    add("Unpack attachments", "edit", "",
        mState.primary() && canUnpackAttachments(*mState.primary()), [this] {
            if (const AuthorId* id = mState.primary())
                unpackAttachments(*id);
        },
        "make a compound kit piece's parts editable");
    add(mState.isolating() ? "Leave isolation" : "Isolate selection", "view",
        "Esc", mState.isolating() || mState.primary() != nullptr, [this] {
            if (mState.isolating())
                leaveIsolation();
            else if (const AuthorId* id = mState.primary())
                enterIsolation(*id);
        },
        "edit one object alone on the grid");
    add("Console", "view", "`", true,
        [this] { mBottom.toggle(BottomTab::Output); });
    add("Problems", "view", "", true,
        [this] { mBottom.toggle(BottomTab::Problems); });
    add("Timeline", "view", "", true,
        [this] { mBottom.toggle(BottomTab::Timeline); });
    for (int i = 0; i < kMainScreenCount; ++i) {
        const MainScreen screen = MainScreen(i);
        add(std::string("Main screen: ") + mainScreenName(screen), "view",
            "Ctrl+" + std::to_string(i + 1), true,
            [this, screen] { setMainScreen(screen); });
    }
    add("Shortcuts", "view", "F1", true, [this] { mHelpOpen = !mHelpOpen; });
    add("Settings", "view", "", true,
        [this] { mSettingsOpen = !mSettingsOpen; });
    // Findable by what it is for, not only by what it is called: "autosave" is
    // the word somebody types when they want this window.
    add("Autosave settings", "view", "", true,
        [this] { mSettingsOpen = true; });
    add(mShowEntityGizmos ? "Hide entity marks" : "Show entity marks", "view",
        "", true, [this] { mShowEntityGizmos = !mShowEntityGizmos; });
    add(mShowFrameStats ? "Hide frame stats" : "Show frame stats", "view", "",
        true, [this] { mShowFrameStats = !mShowFrameStats; });
    for (const eng::RenderPresetInfo& preset : eng::renderPresets()) {
        add(
            std::string("Render preset: ") + preset.name, "view", "",
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
    gameplay("npc", Gameplay::Npc);
    gameplay("trigger volume", Gameplay::Trigger);
    gameplay("point light", Gameplay::PointLight);
    gameplay("directional light", Gameplay::DirectionalLight);

    // Every kit piece, by name. This is the half of the palette that pays for
    // it: the catalogue is hundreds of pieces behind a role header, and an
    // author who knows the piece is called `wall_arch` should not have to find
    // which role it was filed under.
    for (const std::string& role : mState.catalog.roles()) {
        for (const KitPiece* piece : mState.catalog.byRole(role)) {
            add(
                std::string("Place ") + piece->id, "place", "", true,
                [this, id = piece->id] {
                    mState.brush.kind = Brush::Kind::Piece;
                    mState.brush.prefab = id;
                    mState.tool = Tool::Place;
                },
                role);
        }
    }
    return actions;
}

void EditorApp::onGui(const eng::FrameContext& f)
{
    handleShortcuts(f);

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

    // The top bar: menus, the main-screen switcher, the play controls. One row,
    // three zones, laid out by ed::layoutTopBar.
    drawTopBar(f);
    // Open scenes, directly under it and spanning the window -- the tabs are
    // about the whole editor, not about the panel they sit over.
    drawSceneTabBar();

    // What is left is shared between the dockspace, the bottom panel and the
    // status line, in that order from the top.
    const ImVec2 content = ImGui::GetContentRegionAvail();
    // One row at the foot, holding the bottom panel's buttons on the left and
    // the scene context on the right. Godot's arrangement, and it saves the row
    // a separate button strip would have cost.
    const float statusHeight =
        ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    const float workspaceHeight = std::max(content.y - statusHeight, 1.0f);
    // A minimum of roughly six rows of text: below that the console shows its
    // toolbar and nothing else, which is a panel pretending to be open.
    const float bottomHeight = bottomPanelHeight(
        mBottom, workspaceHeight, ImGui::GetTextLineHeightWithSpacing() * 6.0f);
    const ImVec2 dockSize(content.x,
                          std::max(workspaceHeight - bottomHeight, 1.0f));

    // The version is bumped whenever the shipped topology changes, because a
    // saved .ini wins over the builder: without a new id, an author who has run
    // the editor once keeps the old layout forever and never sees the fix.
    //
    // v4: the toolbar is tall enough for its own tab bar plus two wrapped rows
    // (it was being clipped by the panel below), and Hierarchy became a tab
    // beside Asset Browser rather than a stack under it.
    // v5: the Layers panel joined the left rail. The version is part of the id
    // so a saved v4 layout -- which has no node for the new window and would
    // leave it floating over the viewport -- cannot override the new topology.
    // v6: the Timeline joined the bottom rail beside Problems and Console, for
    // exactly the same reason -- a saved v5 layout has no node for it.
    // v7: the Godot workspace. Four of the six windows changed name, the
    // Command Bar stopped existing, Contract and History are new, and the whole
    // bottom rail left the dockspace -- a v6 layout can satisfy none of it.
    const ImGuiID dock = ImGui::GetID("##raven_editor_dock_v7");
    if (!mLayoutBuilt || mResetLayoutRequested) {
        const bool missing = ImGui::DockBuilderGetNode(dock) == nullptr ||
                             ImGui::DockBuilderGetNode(dock)->IsEmpty();
        if (missing || mResetLayoutRequested) {
            buildEditorWorkspace(dock, dockSize.x, dockSize.y, mAppliedUiScale);
            if (materialMode())
                mAssetBrowserModeRequest = 1; // Materials
        }
        mLayoutBuilt = true;
        mResetLayoutRequested = false;
    }
    // No passthru: the central node is the viewport image, not a window onto
    // whatever the main camera happens to be pointing at.
    ImGui::DockSpace(dock, dockSize, ImGuiDockNodeFlags_None);
    drawBottomPanel(f, bottomHeight);
    drawStatusBar();
    ImGui::End();

    if (mFocusPanelFrames > 0)
        --mFocusPanelFrames;

    drawViewport(f);
    // Deferred from the environment hook: framing an elevation needs the open
    // document and a measured viewport, and neither exists when onLoad reads
    // the variable. Applied after the first viewport pass, once.
    if (mPendingView && mViewportW > 8.0f) {
        setViewProjection(*mPendingView);
        mPendingView.reset();
    }
    drawUiStage();
    drawOutliner();
    drawLayers();
    drawInspector();
    drawContract();
    drawHistory();
    // Browser owns shared preview RTT when visible; draw it after Inspector so
    // Materials/Effects mode cannot show one subject under another label.
    drawAssetBrowser();
    drawHelp();
    drawSettings();
    captureSettings();
    drawNewScenePopup();
    drawProjectPopup();
    drawSaveAsPopup();
    drawLayerIoPopup();
    drawImportModelPopup();
    drawOpenScenePopup();
    drawDiscardPopup();
    ConfirmDialog::draw();
    runSelfTest(f.engine);
    // Last, and over everything: the palette is the only surface that is not
    // part of the workspace.
    if (mPalette.open)
        drawCommandPalette(mPalette, paletteActions());

    // GUI widgets mutate authored data after onUpdate() has already mirrored
    // the document. A final revision-aware sync removes the otherwise visible
    // one-frame delay between an inspector/gizmo edit and the 3D viewport.
    if (!materialMode() && mPreview)
        mPreview->sync(mState.document, mState.catalog, mState.assetRoot);
}

// The top bar: one row, three zones.
//
// Menus at the left, the main-screen switcher centred on the *window* and the
// play controls hard right -- Godot's, and the reason to copy it exactly is
// that the switcher's position is what makes it findable. Centred on the space
// the menus leave over, it would slide sideways every time a menu name changed
// length, and an author would have to look for it each session.
//
// The two moving zones are measured before anything is drawn, because ImGui
// lays a menu bar out left to right and cannot reserve space it has not
// reached yet.
void EditorApp::drawTopBar(const eng::FrameContext& f)
{
    if (!ImGui::BeginMenuBar())
        return;

    drawMenuBar(f);
    const float menusEnd = ImGui::GetCursorPosX();

    const float switcherWidth = drawMainScreenSwitcher(true);
    const float playWidth = drawPlayControls(true);
    const TopBarLayout layout =
        layoutTopBar(ImGui::GetWindowWidth(), menusEnd, switcherWidth,
                     playWidth, ImGui::GetStyle().ItemSpacing.x * 2.0f);

    if (layout.switcherFits) {
        ImGui::SetCursorPosX(layout.switcherX);
        drawMainScreenSwitcher(false);
    }
    ImGui::SetCursorPosX(layout.playX);
    drawPlayControls(false);

    ImGui::EndMenuBar();
}

float EditorApp::drawMainScreenSwitcher(bool measureOnly)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    static constexpr MainScreen kScreens[kMainScreenCount] = {
        MainScreen::Scene3D, MainScreen::Screen2D, MainScreen::Material};

    if (measureOnly) {
        float width = 0.0f;
        for (int i = 0; i < kMainScreenCount; ++i) {
            width += ImGui::CalcTextSize(mainScreenName(kScreens[i])).x +
                     style.FramePadding.x * 2.0f;
            if (i > 0)
                width += style.ItemSpacing.x;
        }
        return width;
    }

    for (int i = 0; i < kMainScreenCount; ++i) {
        if (i > 0)
            ImGui::SameLine();
        const MainScreen screen = kScreens[i];
        const bool active = mScreen == screen;
        // The selected screen wears the accent, which is the one place in this
        // bar that colour means "you are here". Everything else is grey.
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_Header));
        if (ImGui::Button(mainScreenName(screen)))
            setMainScreen(screen);
        if (active)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s  [Ctrl+%d]\n%s", mainScreenName(screen),
                              i + 1, mainScreenSummary(screen));
        }
    }
    return 0.0f;
}

// Play, Stop and Cook, hard right. Play is refused -- visibly, with the reason
// -- when the contract says nothing in the scene can be looked through: that is
// the failure this whole vocabulary exists to make loud, and letting F5 launch
// a black window instead is how it stayed quiet.
float EditorApp::drawPlayControls(bool measureOnly)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool running = mPlaytest.running();
    const char* playLabel = running ? "Stop" : "Play";

    if (measureOnly) {
        return ImGui::CalcTextSize(playLabel).x + style.FramePadding.x * 2.0f +
               style.ItemSpacing.x + ImGui::CalcTextSize("Cook").x +
               style.FramePadding.x * 2.0f;
    }

    const ContractReport& report = contract();
    const bool blocked = !running && !report.playable;
    ImGui::BeginDisabled(blocked);
    if (blocked)
        ImGui::PushStyleColor(ImGuiCol_Text, kUiDanger);
    else if (running)
        ImGui::PushStyleColor(ImGuiCol_Button, kUiWarning);
    if (ImGui::Button(playLabel))
        runPlaytest();
    if (blocked || running)
        ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (blocked) {
            ImGui::SetTooltip("This scene has nothing to look through.\n"
                              "The Contract panel says what is missing, and "
                              "has the button that fills it.");
        }
        else {
            ImGui::SetTooltip(running ? "F5 -- stop the running playtest"
                                      : "F5 -- save, cook and launch this "
                                        "scene");
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cook")) {
        std::string mapPath;
        cookScene(mapPath);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("F6 -- cook to a .map without launching");
    return 0.0f;
}

// Open scenes.
//
// The tabs are the visible half of the thing that made opening a scene stop
// being destructive (see SceneTabs.h). The invisible half is that mState is
// only ever a mirror of the active tab, which is why no panel below here had to
// change.
void EditorApp::drawSceneTabBar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ImGui::GetStyle().FramePadding.x,
                               ImGui::GetStyle().FramePadding.y * 0.8f));
    if (ImGui::BeginTabBar("##scene_tabs",
                           ImGuiTabBarFlags_FittingPolicyScroll |
                               ImGuiTabBarFlags_TabListPopupButton)) {
        // Resolved after the loop: activating or closing inside it would
        // reallocate the tab vector while the bar is still walking it.
        std::size_t activate = mTabs.size();
        std::size_t close = mTabs.size();

        for (std::size_t i = 0; i < mTabs.size(); ++i) {
            const SceneTab& tab = mTabs.at(i);
            // The uid, not the index, is the imgui id: closing a tab shifts
            // every index after it, and a positional id would make the
            // survivors inherit each other's scroll and selection state.
            const std::string label =
                sceneTabName(tab) + (tab.dirty ? "  \xe2\x80\xa2" : "") +
                "###scene_tab" + std::to_string(tab.uid);
            bool open = true;
            ImGuiTabItemFlags flags = 0;
            if (i == mTabs.active() && mTabBarFollowsActive)
                flags |= ImGuiTabItemFlags_SetSelected;
            if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                if (i != mTabs.active())
                    activate = i;
                ImGui::EndTabItem();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s%s", sceneTabTooltip(tab).c_str(),
                                  tab.dirty ? "\nunsaved changes" : "");
            }
            if (!open)
                close = i;
        }
        mTabBarFollowsActive = false;

        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing |
                                          ImGuiTabItemFlags_NoTooltip))
            mNewSceneOpen = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("New scene  [Ctrl+N]");

        ImGui::EndTabBar();

        if (activate < mTabs.size())
            activateTab(activate);
        if (close < mTabs.size())
            requestCloseTab(close);
    }
    ImGui::PopStyleVar();
}

// The bottom panel's body and the handle that resizes it. The buttons that open
// it live in the status row below (drawStatusBar), which is where Godot puts
// them and is one row cheaper than a strip of their own.
// --- script errors ----------------------------------------------------------
//
// The playtest is a separate process by design (see RunGame.h), so there is no
// in-memory channel between the running game and the editor. Its stdout is
// already redirected to a log; reading the errors back out of that is the whole
// mechanism, and it costs the runtime nothing.

void EditorApp::refreshScriptIssues()
{
    if (mPlaytestLogPath.empty())
        return;
    mScriptIssues = parseScriptIssues(tailFile(mPlaytestLogPath));
}

void EditorApp::drawScriptIssues()
{
    ImGui::TextDisabled("errors the running game reported");
    ImGui::SameLine();
    if (ImGui::SmallButton("refresh"))
        refreshScriptIssues();
    if (!mPlaytestLogPath.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", mPlaytestLogPath.c_str());
    }
    ImGui::Separator();

    if (mScriptIssues.empty()) {
        // Three different nothings, because they mean different things to
        // somebody staring at an empty panel.
        if (mPlaytest.running())
            ImGui::TextDisabled("playtest running -- no script errors so far.");
        else if (mScriptIssuesFromCurrentRun)
            ImGui::TextDisabled("the last playtest reported no script errors.");
        else
            ImGui::TextDisabled("press F5 to play; script errors appear here.");
        return;
    }

    for (int i = 0; i < int(mScriptIssues.size()); ++i) {
        const ScriptIssue& issue = mScriptIssues[std::size_t(i)];
        ImGui::PushID(i);

        const std::string header =
            issue.script + " -- " +
            (issue.callback.empty() ? std::string("?") : issue.callback) + "()";
        ImGui::TextColored(kUiDanger, "%s", header.c_str());
        if (!issue.subject.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("on %s", issue.subject.c_str());
        }
        ImGui::TextWrapped("%s", issue.message.c_str());

        // The script is a path the editor can act on: opening it is the next
        // thing anybody does after reading the error, and making them find it
        // in the file browser is the reason this panel would go unused.
        if (ImGui::SmallButton("open script")) {
            const std::filesystem::path file =
                std::filesystem::path(mState.assetRoot) / issue.script;
            std::string error;
            if (!openInExternalEditor(file.string(), error))
                mStatus = error;
        }
        if (!issue.detail.empty()) {
            ImGui::SameLine();
            if (ImGui::TreeNode("traceback")) {
                ImGui::TextUnformatted(issue.detail.c_str());
                ImGui::TreePop();
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    }
}

void EditorApp::drawBottomPanel(const eng::FrameContext& f, float height)
{
    if (height <= 0.0f)
        return;

    // The drag handle, in the gap the dockspace above just ended at.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::InvisibleButton("##bottom_splitter",
                           ImVec2(-1.0f, std::max(4.0f, 5.0f * mAppliedUiScale)));
    ImGui::PopStyleVar();
    const bool hovered = ImGui::IsItemHovered();
    mBottomResizing = ImGui::IsItemActive();
    if (hovered || mBottomResizing)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (mBottomResizing) {
        // Clamped here, not only in bottomPanelHeight: that clamps the value it
        // RETURNS, so dragging past the floor kept driving `height` down
        // without bound and the panel then refused to grow until the mouse had
        // travelled all of it back. The bounds are the panel's own, so this
        // cannot disagree with what gets drawn.
        const float viewport = ImGui::GetMainViewport()->WorkSize.y;
        mBottom.height =
            std::clamp(mBottom.height - ImGui::GetIO().MouseDelta.y, 64.0f,
                       std::max(viewport * 0.5f, 64.0f));
    }
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
        ImGui::GetColorU32(mBottomResizing ? ImGuiCol_SeparatorActive
                           : hovered      ? ImGuiCol_SeparatorHovered
                                          : ImGuiCol_Separator));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(8.0f * mAppliedUiScale, 6.0f * mAppliedUiScale));
    if (ImGui::BeginChild("##bottom_body", ImVec2(0.0f, height),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        switch (BottomTab(mBottom.open)) {
        case BottomTab::Output:
            mConsole.drawBody();
            break;
        case BottomTab::Problems:
            drawIssues();
            break;
        case BottomTab::Timeline:
            drawTimeline();
            break;
        case BottomTab::Scripts:
            drawScriptIssues();
            break;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    (void)f;
}

void EditorApp::drawMenuBar(const eng::FrameContext& f)
{
    // "Scene" rather than "File", which is Godot's name for it and the more
    // honest one: everything in it is about the open scene, and the editor has
    // no other kind of document to confuse it with.
    // A project is a *different* document from a scene -- it is the game, and
    // the scenes are its contents -- so it gets its own menu rather than three
    // more entries under Scene. Godot draws the same line.
    if (ImGui::BeginMenu("Project")) {
        if (ImGui::MenuItem("New project...")) {
            mNewProjectOpen = true;
            std::snprintf(mProjectPath, sizeof(mProjectPath), "%s",
                          (std::filesystem::path(
                               eng::assets::project() / "projects" / "my-game")
                               .string())
                              .c_str());
            std::snprintf(mProjectName, sizeof(mProjectName), "My Game");
        }
        if (ImGui::MenuItem("Open project...")) {
            mOpenProjectOpen = true;
            mProjectPath[0] = '\0';
        }
        if (ImGui::BeginMenu("Open recent project",
                             !mProject.recents().empty())) {
            for (const std::string& dir : mProject.recents()) {
                const std::string name =
                    std::filesystem::path(dir).filename().string();
                if (ImGui::MenuItem(name.c_str()))
                    openProject(dir);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", dir.c_str());
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Export...", nullptr, false, mProject.isOpen()))
            exportProjectBuild();
        if (ImGui::MenuItem("Close project", nullptr, false,
                            mProject.isOpen()))
            closeProject();
        if (mProject.isOpen()) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", mProject.project().name.c_str());
            ImGui::TextDisabled("%s", mProject.project().dir.c_str());
            ImGui::TextDisabled("plays on raven_player");
        } else {
            ImGui::Separator();
            ImGui::TextDisabled("no project -- editing the shipped content");
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Scene")) {
        if (ImGui::MenuItem("New scene...", "Ctrl+N"))
            mNewSceneOpen = true;
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            mOpenSceneOpen = true;
        if (ImGui::MenuItem("Close scene", "Ctrl+W"))
            requestCloseTab(mTabs.active());
        if (ImGui::MenuItem("Import model...")) {
            mImportModelOpen = true;
            // Nothing preselected: the dialog lists what is actually there now,
            // where it used to prefill a path to a file that never existed.
            mImportModelPath.clear();
            mImportFilter[0] = '\0';
        }
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
        // Switching between what is already open. The tab bar is the fast way;
        // this exists so the set is reachable without a mouse, and so a very
        // narrow window (where the bar scrolls) still lists all of them.
        if (ImGui::BeginMenu("Open scenes", mTabs.size() > 1)) {
            for (std::size_t i = 0; i < mTabs.size(); ++i) {
                const SceneTab& tab = mTabs.at(i);
                const std::string label =
                    sceneTabName(tab) + (tab.dirty ? "  *" : "");
                if (ImGui::MenuItem(label.c_str(), nullptr,
                                    i == mTabs.active()))
                    activateTab(i);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S"))
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
        if (ImGui::MenuItem("Recover autosave", nullptr, false,
                            autosaveIsStale(mState.scenePath,
                                            mState.assetRoot + "/scenes")))
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
        if (ImGui::MenuItem(redo.c_str(), "Ctrl+Y / Ctrl+Shift+Z", false,
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
        ImGui::EndMenu();
    }
    // Godot's Debug menu: what a playtest is launched WITH. The launch itself
    // is the button at the top right, not an item buried in a menu.
    if (ImGui::BeginMenu("Debug")) {
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
            ImGui::SetTooltip(
                "Off: start at the scene's player spawn, which is "
                "how the arrival itself gets checked.");
        bool settingsChanged = false;
        settingsChanged |= ImGui::MenuItem("Match viewport look", nullptr,
                                           &mSettings.playtestMatchesViewport);
        if (ImGui::BeginMenu("Playtest look",
                             !mSettings.playtestMatchesViewport)) {
            if (ImGui::MenuItem("Game default", nullptr,
                                mSettings.playtestPreset.empty())) {
                mSettings.playtestPreset.clear();
                settingsChanged = true;
            }
            for (const eng::RenderPresetInfo& preset : eng::renderPresets()) {
                if (ImGui::MenuItem(preset.name, nullptr,
                                    mSettings.playtestPreset == preset.name)) {
                    mSettings.playtestPreset = preset.name;
                    settingsChanged = true;
                }
            }
            ImGui::EndMenu();
        }
        settingsChanged |= ImGui::MenuItem("Open debug console", nullptr,
                                           &mSettings.playtestConsole);
        settingsChanged |= ImGui::MenuItem("Show colliders", nullptr,
                                           &mSettings.playtestColliders);
        settingsChanged |= ImGui::MenuItem("Fullscreen", nullptr,
                                           &mSettings.playtestFullscreen);
        if (settingsChanged)
            commitSettings();
        ImGui::EndMenu();
    }
    // Selection and placement aids, Gregory §15.4.1.4 and §15.4.1.7. Their own
    // menu rather than more of Edit: everything here acts on the selection as a
    // set, which is a different question from the single-entity verbs above.
    if (ImGui::BeginMenu("Arrange")) {
        const bool many = mState.selection.size() > 1;
        const bool any = !mState.selection.empty();
        if (ImGui::BeginMenu("Align", many)) {
            struct Row { const char* label; align::Mode mode; };
            static const Row kRows[] = {{"min", align::Mode::Min},
                                        {"centre", align::Mode::Centre},
                                        {"max", align::Mode::Max}};
            for (const align::Axis axis :
                 {align::Axis::X, align::Axis::Y, align::Axis::Z}) {
                if (!ImGui::BeginMenu(align::axisName(axis)))
                    continue;
                for (const Row& row : kRows)
                    if (ImGui::MenuItem(row.label))
                        alignSelection(axis, row.mode);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Distribute evenly",
                             mState.selection.size() > 2)) {
            for (const align::Axis axis :
                 {align::Axis::X, align::Axis::Y, align::Axis::Z})
                if (ImGui::MenuItem(align::axisName(axis)))
                    distributeSelection(axis);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Drop to surface", nullptr, false, any))
            dropSelectionToSurface();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("rests each entity's underside on whatever is "
                              "below it");

        ImGui::Separator();
        // Named selections: the chapter's "selections to be named and saved
        // for later retrieval".
        if (ImGui::BeginMenu("Saved selections")) {
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##setname", "name",
                                     mSelectionSetName,
                                     sizeof(mSelectionSetName));
            ImGui::SameLine();
            if (ImGui::SmallButton("Save") && any) {
                selection::save(mSelectionSets, mSelectionSetName,
                                mState.selection);
                mStatus = "saved selection '" +
                          std::string(mSelectionSetName) + "'";
                mSelectionSetName[0] = '\0';
            }
            if (mSelectionSets.empty()) {
                ImGui::TextDisabled("none yet");
            }
            for (const selection::SelectionSet& set : mSelectionSets) {
                ImGui::PushID(set.name.c_str());
                if (ImGui::MenuItem(set.name.c_str())) {
                    mState.selection =
                        selection::restore(mSelectionSets, set.name,
                                           mState.document);
                    mPickCycle.reset();
                    mSelectionAnchor.clear();
                    if (!mState.selection.empty())
                        mOutlinerReveal = mState.selection.front();
                    mStatus = std::to_string(mState.selection.size()) +
                              " restored from '" + set.name + "'";
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    const std::string name = set.name;
                    ImGui::PopID();
                    selection::remove(mSelectionSets, name);
                    break;
                }
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    // "Editor": everything about the tool rather than about the scene, which is
    // the split Godot draws and the one that stops this from being a Window
    // menu with the preferences hidden at the bottom of it.
    if (ImGui::BeginMenu("Editor")) {
        if (ImGui::MenuItem("Command palette", "Ctrl+P"))
            openPalette(mPalette);
        ImGui::MenuItem("Editor settings...", nullptr, &mSettingsOpen);
        ImGui::Separator();
        if (ImGui::BeginMenu("Main screen")) {
            for (int i = 0; i < kMainScreenCount; ++i) {
                const MainScreen screen = MainScreen(i);
                const std::string key = "Ctrl+" + std::to_string(i + 1);
                if (ImGui::MenuItem(mainScreenName(screen), key.c_str(),
                                    mScreen == screen))
                    setMainScreen(screen);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Docks")) {
            if (ImGui::MenuItem("Scene tree", "Ctrl+4"))
                requestPanelFocus("outliner");
            if (ImGui::MenuItem("FileSystem", "Ctrl+5"))
                requestPanelFocus("catalog");
            if (ImGui::MenuItem("Inspector", "Ctrl+6"))
                requestPanelFocus("inspector");
            if (ImGui::MenuItem("Contract", "Ctrl+7"))
                requestPanelFocus("contract");
            if (ImGui::MenuItem("Layers", "Ctrl+8"))
                requestPanelFocus("layers");
            if (ImGui::MenuItem("History"))
                requestPanelFocus("history");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Bottom panel")) {
            for (int i = 0; i < kBottomTabCount; ++i) {
                const BottomTab tab = BottomTab(i);
                if (ImGui::MenuItem(bottomTabName(tab),
                                    tab == BottomTab::Output ? "`" : nullptr,
                                    mBottom.isOpen(tab)))
                    mBottom.toggle(tab);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reset workspace"))
            mResetLayoutRequested = true;
        ImGui::EndMenu();
    }
    // Everything about what the viewport SHOWS, which is a question about
    // looking rather than about editing -- and the reason the Command Bar panel
    // stopped existing. Duplicated deliberately in the viewport's own toolbar,
    // where an author flipping them while placing actually reaches for them;
    // this copy is for discovery and for the keyboard.
    if (ImGui::BeginMenu("View")) {
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
        if (ImGui::MenuItem("Scene lighting", nullptr, mGameLighting)) {
            mGameLighting = !mGameLighting;
            if (mEngine)
                applySceneEnvironment(mEngine->renderer());
        }
        ImGui::Separator();
        ImGui::MenuItem("Entity marks", nullptr, &mShowEntityGizmos);
        ImGui::MenuItem("Trigger and light volumes", nullptr,
                        &mShowGizmoVolumes);
        ImGui::MenuItem("Frame stats", nullptr, &mShowFrameStats);
        ImGui::MenuItem("Viewport grid", nullptr, &mShowGrid);
        ImGui::Separator();
        ImGui::MenuItem("Snap to grid", "G", &mState.gridState.snap);
        if (ImGui::MenuItem("Frame selection", "F"))
            frameSelectionOrAll();
        if (ImGui::MenuItem("Walk the scene", "V", mState.camera.walking()))
            toggleWalk();
        ImGui::Separator();
        // Navigation history and bookmarks, Gregory §15.4.1.3.
        if (ImGui::MenuItem("Back", "Alt+Left", false,
                            mCameraHistory.canGoBack()))
            navigateHistory(false);
        if (ImGui::MenuItem("Forward", "Alt+Right", false,
                            mCameraHistory.canGoForward()))
            navigateHistory(true);
        if (ImGui::BeginMenu("Saved views")) {
            for (std::size_t slot = 0; slot < mBookmarks.size(); ++slot) {
                ImGui::PushID(int(slot));
                const std::string key =
                    "Alt+" + std::to_string((slot + 1) % 10);
                if (mBookmarks.has(slot)) {
                    if (ImGui::MenuItem(mBookmarks.at(slot).name.c_str(),
                                        key.c_str()))
                        jumpToBookmark(slot);
                } else {
                    const std::string label =
                        "set view " + std::to_string(slot + 1);
                    if (ImGui::MenuItem(label.c_str(), key.c_str()))
                        setBookmark(slot);
                }
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Keyboard shortcuts", "F1", &mHelpOpen);
        ImGui::EndMenu();
    }
}

// The viewport's own toolbar: one row across the top of the 3D view.
//
// This used to be two strips in two places -- a docked "Command Bar" panel at
// the very top of the window holding the tools, the grid and the transform
// space, and a second row inside the viewport holding the view toggles. The
// split was defensible on paper (edits above, ways of looking below) and
// unusable in practice: choosing a tool and choosing what that tool snaps to
// are one thought, and they were four hundred pixels apart with a scene view
// between them.
//
// Godot's answer, and now this one: everything that acts on the viewport lives
// on the viewport, and the row stays short by putting the settings behind three
// named menus rather than spelling every one of them out.
//
//   [select][place][room] | [move][rotate][scale] | Transform | Snap | View
//                                     ... [3D][Top][Front][Side] [Frame][Walk]
namespace {

// A toolbar menu: a button that opens a popup under itself. Godot's viewport
// menus, and the reason the row fits on a 1280px window.
bool toolbarMenu(const char* label)
{
    if (ImGui::Button(label))
        ImGui::OpenPopup(label);
    return ImGui::BeginPopup(label);
}

} // namespace

void EditorApp::drawViewportToolbar()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

    const float icon = 18.0f;

    // --- what the pointer does -----------------------------------------------
    if (iconButton(Icon::Select, "##tool_select",
                   "Select  [Q]\nPick without transform handles",
                   mState.tool == Tool::Select && mGizmoOperation < 0, icon)) {
        mState.tool = Tool::Select;
        mGizmoOperation = -1;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(materialMode() || mGizmoDragging);
    if (iconButton(Icon::Move, "##gizmo_move",
                   "Move  [W]\nTranslate the selection",
                   mState.tool == Tool::Select && mGizmoOperation == 0, icon)) {
        mState.tool = Tool::Select;
        mGizmoOperation = 0;
    }
    ImGui::SameLine();
    if (iconButton(Icon::Rotate, "##gizmo_rotate",
                   "Rotate  [E]\nRotate the selection",
                   mState.tool == Tool::Select && mGizmoOperation == 1, icon)) {
        mState.tool = Tool::Select;
        mGizmoOperation = 1;
    }
    ImGui::SameLine();
    if (iconButton(Icon::Scale, "##gizmo_scale",
                   "Scale  [R]\nScale the selection",
                   mState.tool == Tool::Select && mGizmoOperation == 2, icon)) {
        mState.tool = Tool::Select;
        mGizmoOperation = 2;
    }
    ImGui::EndDisabled();

    // The viewport cannot scroll -- it is an image, and a scrollbar over it
    // would move the image rather than the toolbar -- so every run below wraps
    // instead. The alternative is the last control silently falling off a
    // narrow viewport, which is where "Room" and the walk toggle used to go.
    if (ui::sameLineIfItFits(ui::buttonWidth("|") +
                             ui::iconRowWidth(icon, 2))) {
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // --- what the pointer builds ---------------------------------------------
    ImGui::BeginDisabled(materialMode());
    if (iconButton(Icon::Place, "##tool_place",
                   "Place  [P]\nPaint the FileSystem panel's current brush",
                   mState.tool == Tool::Place, icon))
        mState.tool = Tool::Place;
    ImGui::SameLine();
    if (iconButton(Icon::Room, "##tool_room",
                   "Room  [B]\nDrag out a complete room rectangle",
                   mState.tool == Tool::Room, icon))
        mState.tool = Tool::Room;
    ImGui::EndDisabled();

    if (ui::sameLineIfItFits(ui::buttonWidth("|") +
                             ui::buttonWidth("Transform"))) {
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // --- Transform ------------------------------------------------------------
    // The gizmo's frame of reference, and the batch operations that move a
    // selection without one. Both answer "how does this move", which is why
    // they are one menu rather than a toggle and a top-level menu.
    if (toolbarMenu("Transform")) {
        if (materialMode()) {
            ImGui::TextDisabled("the stage turns about Y");
        }
        else if (mGizmoOperation < 0) {
            ImGui::TextDisabled("pick only -- choose Move, Rotate or Scale");
        }
        else {
            const bool worldForced = mState.selection.size() > 1;
            const bool localForced = mGizmoOperation == 2 && !worldForced;
            ImGui::BeginDisabled(mGizmoDragging || worldForced || localForced);
            if (ImGui::MenuItem("World axes", "X", !mGizmoLocal))
                mGizmoLocal = false;
            if (ImGui::MenuItem("Local axes", "X", mGizmoLocal))
                mGizmoLocal = true;
            ImGui::EndDisabled();
            if (worldForced)
                ImGui::TextDisabled("multiple objects always use world axes");
            else if (localForced)
                ImGui::TextDisabled("scale is always local");
        }
        ImGui::Separator();
        const bool many = mState.selection.size() > 1;
        const bool any = !mState.selection.empty();
        if (ImGui::BeginMenu("Align", many)) {
            struct Row { const char* label; align::Mode mode; };
            static const Row kRows[] = {{"min", align::Mode::Min},
                                        {"centre", align::Mode::Centre},
                                        {"max", align::Mode::Max}};
            for (const align::Axis axis :
                 {align::Axis::X, align::Axis::Y, align::Axis::Z}) {
                if (!ImGui::BeginMenu(align::axisName(axis)))
                    continue;
                for (const Row& row : kRows)
                    if (ImGui::MenuItem(row.label))
                        alignSelection(axis, row.mode);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Distribute evenly", mState.selection.size() > 2)) {
            for (const align::Axis axis :
                 {align::Axis::X, align::Axis::Y, align::Axis::Z})
                if (ImGui::MenuItem(align::axisName(axis)))
                    distributeSelection(axis);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Drop to surface", nullptr, false, any))
            dropSelectionToSurface();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("rests each entity's underside on whatever is "
                              "below it");
        ImGui::EndPopup();
    }

    // --- Snap -----------------------------------------------------------------
    // The grid step, the work plane and the storey cut. The button's own label
    // is the live step, so the setting is readable without opening it -- a
    // coarse grid that silently changed meaning is exactly the failure the
    // "(drawn x2)" note below was written for.
    ui::sameLineIfItFits(ui::buttonWidth("Snap 0.25 m"));
    {
        char label[64];
        std::snprintf(label, sizeof(label), "%s %.2g m###snap",
                      mState.gridState.snap ? "Snap" : "Free",
                      double(mState.gridState.step()));
        if (mState.gridState.snap)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_Header));
        const bool open = toolbarMenu(label);
        if (mState.gridState.snap)
            ImGui::PopStyleColor();
        if (open) {
            ImGui::Checkbox("snap to grid  [G]", &mState.gridState.snap);
            eng::imguihint::hover(
                "editor.grid.snap",
                "Off, pieces land exactly where the cursor is.");
            ImGui::Separator();
            ImGui::Text("grid  %.2g m", double(mState.gridState.step()));
            if (mGridMultiple > 1) {
                // The drawn grid coarsened with the camera's height. Say so: a
                // line spacing that silently changes meaning is worse than no
                // grid at all.
                ImGui::SameLine();
                ImGui::TextDisabled("(drawn x%d)", mGridMultiple);
            }
            if (ImGui::SmallButton("coarser  [ [ ]"))
                mState.gridState.coarser();
            ImGui::SameLine();
            if (ImGui::SmallButton("finer  [ ] ]"))
                mState.gridState.finer();
            ImGui::Separator();
            ImGui::Text("work plane  %.1f m", double(mState.gridState.level));
            if (ImGui::SmallButton("down a storey"))
                mState.gridState.level -= mState.grid.cell;
            ImGui::SameLine();
            if (ImGui::SmallButton("up a storey"))
                mState.gridState.level += mState.grid.cell;
            ImGui::Checkbox("hide everything above it",
                            &mState.gridState.cutAboveLevel);
            eng::imguihint::hover(
                "editor.grid.cutabove",
                "Off, the viewport shows every storey; the level control then "
                "only moves the grid and what new pieces snap to.");
            ImGui::EndPopup();
        }
    }

    // --- View -----------------------------------------------------------------
    // Everything that changes what is drawn without changing the document.
    ui::sameLineIfItFits(ui::buttonWidth("View"));
    if (toolbarMenu("View")) {
        ImGui::MenuItem("Entity marks", nullptr, &mShowEntityGizmos);
        ImGui::MenuItem("Trigger and light volumes", nullptr,
                        &mShowGizmoVolumes);
        ImGui::MenuItem("Frame stats", nullptr, &mShowFrameStats);
        ImGui::MenuItem("Grid", nullptr, &mShowGrid);
        ImGui::Separator();
        // Lighting is the one switch here that costs something to apply, so it
        // is pushed to the renderer on the frame it changes rather than every
        // frame.
        if (ImGui::MenuItem("Scene lighting", nullptr, mGameLighting)) {
            mGameLighting = !mGameLighting;
            if (mEngine)
                applySceneEnvironment(mEngine->renderer());
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off: the editor's flat work light, which is what "
                              "placing things needs.\nOn: the level's own "
                              "lighting, which is the only way to see whether "
                              "it guides the eye.");
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
        ImGui::EndPopup();
    }

    // --- right: what the camera is ------------------------------------------
    //
    // Right-aligned because it is the answer to a different question -- "where
    // am I looking from" rather than "what am I doing" -- and because the row
    // then reads outward from both ends instead of trailing off.
    struct Choice {
        EditorCamera::Projection projection;
        const char* label;
        const char* tip;
    };
    static const Choice kChoices[] = {
        {EditorCamera::Projection::Perspective, "3D",
         "perspective -- fly with the right mouse button"},
        {EditorCamera::Projection::Top, "Top",
         "plan view, looking down -- drag to pan, wheel to zoom"},
        {EditorCamera::Projection::Front, "Front",
         "front elevation, looking along -Z"},
        {EditorCamera::Projection::Side, "Side",
         "side elevation, looking along -X"},
    };

    float rightWidth = 0.0f;
    for (const Choice& choice : kChoices) {
        rightWidth += ImGui::CalcTextSize(choice.label).x +
                      style.FramePadding.x * 2.0f + 4.0f;
    }
    rightWidth += ImGui::CalcTextSize("Frame").x + style.FramePadding.x * 2.0f +
                  4.0f + icon + style.FramePadding.x * 2.0f;

    const float rightX = ImGui::GetContentRegionMax().x - rightWidth;
    // Right-aligned when there is room, and on its own row when there is not.
    // Never both: pushing the cursor to a rightX that is behind where the row
    // already reached is what draws two controls on top of each other.
    if (ui::sameLineIfItFits(rightWidth) &&
        rightX > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(rightX);
    }
    else if (ImGui::GetCursorPosX() > 0.0f &&
             ImGui::GetItemRectMax().x > ImGui::GetWindowPos().x) {
        // sameLineIfItFits already left us on a new row; only right-align it
        // when the whole cluster still fits on one.
        if (rightX > 0.0f)
            ImGui::SetCursorPosX(rightX);
    }

    const bool walking = mState.camera.walking();
    ImGui::BeginDisabled(walking);
    for (const Choice& choice : kChoices) {
        const bool active = mState.camera.projection() == choice.projection;
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_Header));
        if (ImGui::Button(choice.label))
            setViewProjection(choice.projection);
        if (active)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", walking
                                        ? "walk mode is always a perspective "
                                          "view"
                                        : choice.tip);
        }
        ImGui::SameLine();
    }
    ImGui::EndDisabled();

    if (ImGui::Button("Frame"))
        frameSelectionOrAll();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("F -- fit the selection, or the whole scene");
    ImGui::SameLine();
    if (iconButton(Icon::Spawn, "##walk",
                   walking ? "standing at the player's eye -- click to leave"
                           : "V -- judge the room from where the player's head "
                             "will be",
                   walking, icon))
        toggleWalk();

    ImGui::PopStyleVar();
    ImGui::Separator();
}

void EditorApp::drawViewportStats(const eng::FrameContext& f)
{
    if (!mShowFrameStats || mViewportW < 8.0f || mViewportH < 8.0f)
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
                   mFrameBudget, mViewportX, mViewportY, mViewportW,
                   mViewportH);

    if (mState.isolating()) {
        const Entity* root = mState.document.find(mState.isolation.root);
        const std::string label =
            root ? (root->name.empty() ? root->id : root->name)
                 : mState.isolation.root;
        // Members minus the root itself: an author counts what is *in* the
        // object, and "1 part" for an object with nothing in it reads as a
        // miscount rather than as an empty one.
        const std::size_t parts = mState.isolation.members.empty()
                                      ? 0
                                      : mState.isolation.members.size() - 1;
        if (drawIsolationBanner(ImGui::GetWindowDrawList(), label, parts,
                                mViewportX, mViewportY, mViewportW,
                                mViewportHovered))
            leaveIsolation();
    }
}

void EditorApp::drawViewport(const eng::FrameContext& f)
{
    focusPanelIfRequested("viewport");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin(workspace_window::kViewport3D, nullptr,
                     kPanelFlags | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse)) {
        // The centre node keeps its own tab bar, and clicking a tab is a
        // perfectly reasonable way to switch the main screen -- so it has to
        // mean the same thing the top bar's switcher means. A docked window
        // that is not the selected tab is skipped by Begin, so "this one drew"
        // is exactly "this one is showing".
        //
        // Read here rather than driven from the tab bar because the dependency
        // only runs one way: the switcher raises a window, and a raised window
        // reports what the switcher should say. Two writers would fight.
        if (mScreen == MainScreen::Screen2D)
            mScreen = MainScreen::Scene3D;
        // Above the image, inside the panel: these toggles answer questions
        // about what is on screen, so they belong next to the screen rather
        // than in a toolbar at the far top of the window.
        drawViewportToolbar();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        mViewportX = pos.x;
        mViewportY = pos.y;
        mViewportW = std::max(size.x, 0.0f);
        mViewportH = std::max(size.y, 0.0f);
        const bool validViewport = mViewportW > 8.0f && mViewportH > 8.0f;
        bool hasViewportImage = false;

        if (validViewport) {
            const ImVec2 framebufferScale =
                ImGui::GetIO().DisplayFramebufferScale;
            const int textureW = std::max(
                1, int(std::lround(mViewportW *
                                   std::max(framebufferScale.x, 1.0f))));
            const int textureH = std::max(
                1, int(std::lround(mViewportH *
                                   std::max(framebufferScale.y, 1.0f))));
            if (textureW != mViewportTextureW ||
                textureH != mViewportTextureH) {
                mViewportTextureW = textureW;
                mViewportTextureH = textureH;
                f.engine.renderer().resizeEditorViewport(textureW, textureH);
            }
            const uint64_t texture =
                f.engine.renderer().editorViewportTextureId();
            if (texture != 0) {
                // Default uv: OGRE's render-to-texture already hands back a
                // top-down image, so flipping V here turned the whole world
                // upside down.
                ImGui::Image(static_cast<ImTextureID>(texture), size);
                hasViewportImage = true;
            }
            else {
                ImGui::TextUnformatted("offscreen viewport unavailable");
            }
        }
        const ImVec2 imageMax(pos.x + mViewportW, pos.y + mViewportH);
        mViewportHovered =
            hasViewportImage &&
            ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            ImGui::IsMouseHoveringRect(pos, imageMax, false);
        if (materialMode() || mState.tool != Tool::Place || !mViewportHovered ||
            mFlying)
            mPreview->hidePlacementGhost();

        ImDrawList* viewportDraw = ImGui::GetWindowDrawList();
        if (validViewport)
            viewportDraw->PushClipRect(pos, imageMax, true);
        // Marks and diagnostics sit below manipulators. A selected light mark
        // must not paint over the transform handle at the same screen point.
        drawEntityGizmos();
        drawViewportStats(f);

        // Keep the active interaction visible where the work happens. Toolbar
        // icons are good controls but poor status: once the pointer is over the
        // scene, this badge answers whether a click will pick, paint or drag.
        if (validViewport) {
            std::string mode;
            if (materialMode()) {
                mode = "MATERIAL STAGE";
            }
            else if (mState.tool == Tool::Place) {
                mode = "PLACE [P]";
                std::string brush =
                    mState.brush.kind == Brush::Kind::Gameplay
                        ? std::string(gameplayName(mState.brush.gameplay))
                    : mState.brush.kind == Brush::Kind::Particles
                        ? mState.brush.effect
                        : mState.brush.prefab;
                if (brush.empty())
                    brush = "no brush";
                constexpr std::size_t kMaxBrushChars = 32;
                if (brush.size() > kMaxBrushChars)
                    brush = brush.substr(0, kMaxBrushChars - 3) + "...";
                mode += "  /  " + brush;
            }
            else if (mState.tool == Tool::Room) {
                mode = "ROOM [B]";
            }
            else if (mGizmoOperation < 0) {
                mode = "SELECT [Q]";
            }
            else if (mGizmoOperation == 1) {
                mode = "ROTATE [E]";
            }
            else if (mGizmoOperation == 2) {
                mode = "SCALE [R]";
            }
            else {
                mode = "MOVE [W]";
            }
            if (mState.camera.walking())
                mode += "  /  WALK VIEW";

            const float padX = 10.0f * mAppliedUiScale;
            const float padY = 6.0f * mAppliedUiScale;
            const ImVec2 textSize = ImGui::CalcTextSize(mode.c_str());
            const ImVec2 badgeMin(pos.x + 12.0f * mAppliedUiScale,
                                  pos.y + 12.0f * mAppliedUiScale);
            const ImVec2 badgeMax(badgeMin.x + textSize.x + padX * 2.0f,
                                  badgeMin.y + textSize.y + padY * 2.0f);
            ImVec4 badgeColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
            badgeColor.w = 0.94f;
            viewportDraw->AddRectFilled(badgeMin, badgeMax,
                                        ImGui::GetColorU32(badgeColor));
            viewportDraw->AddRect(badgeMin, badgeMax,
                                  ImGui::GetColorU32(ImGuiCol_Border));
            viewportDraw->AddText(ImVec2(badgeMin.x + padX, badgeMin.y + padY),
                                  ImGui::GetColorU32(ImGuiCol_Text),
                                  mode.c_str());
        }
        if (materialMode()) {
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
            // Escape abandons the drag. Without it the only way out of a
            // rectangle started by accident was to finish it and undo, and a
            // room is the most expensive thing in the editor to commit.
            if (mRoomDragging && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                mRoomDragging = false;
                mStatus = "room cancelled";
            }
            else if (mRoomDragging &&
                     !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                mRoomDragging = false;
                if (mViewportHovered)
                    commitRoom();
                else
                    mStatus = "room cancelled -- release inside the viewport";
            }
        }
        else if (mState.tool == Tool::Place) {
            const ImGuiIO& io = ImGui::GetIO();
            drawPlacementGhost();
            if (mViewportHovered && !mFlying) {
                // Wheel rotates the brush. The one gesture here that needs no
                // click, so it works while lining a piece up rather than only
                // after committing to one.
                //
                // Ctrl+wheel resizes it instead, and the ghost is drawn at that
                // size -- so "how big should this be" is answered before the
                // click, by looking at the room, rather than after it with the
                // scale gizmo. Ctrl already means "ignore geometry" for the
                // click; the two do not collide because one is a wheel and the
                // other is a button.
                if (io.MouseWheel != 0.0f) {
                    if (io.KeyCtrl)
                        mState.brush.resize(io.MouseWheel > 0.0f ? 1.1f
                                                                 : 1.0f / 1.1f);
                    else
                        mState.brush.rotate(io.MouseWheel > 0.0f ? 1 : -1);
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (io.KeyAlt) {
                        // Sample, not a stroke: the eyedropper changes the
                        // brush and places nothing.
                        pickBrushFrom(hoveredEntity());
                    }
                    else {
                        mStroke = io.KeyShift ? Stroke::Erase : Stroke::Paint;
                        mStrokeSlots.clear();
                        mStrokeParts.clear();
                        mStrokeIds.clear();
                    }
                }
                if (mStroke != Stroke::None &&
                    ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    if (mStroke == Stroke::Erase)
                        eraseAt(hoveredEntity());
                    else
                        placeAt(hoveredPlacement());
                }
            }
            if (mStroke != Stroke::None &&
                !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                finishStroke();
        }
        else {
            // Gizmo before picking: a click that lands on a handle is a drag,
            // not a selection change.
            drawGizmo(f);
            // The band before the pick, for the same reason: it only starts on
            // empty space, and once one is running it owns the drag -- so a
            // sweep across the level cannot also register as a click on
            // whatever it happened to finish over.
            handleMarquee(f);
            if (!mMarqueeDragging)
                handleViewportPicking(f);
        }
        if (validViewport)
            viewportDraw->PopClipRect();
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
    // An elevation builds the same orthographic matrix the renderer does, so a
    // gizmo mark is drawn exactly where a click on it would land -- which is
    // the contract the whole file depends on, and the one that breaks silently
    // if these two ever disagree.
    const float orthoHeight = camera.activeOrthoHeight();
    if (orthoHeight > 0.0f) {
        const float halfHeight = orthoHeight * 0.5f;
        const float halfWidth = halfHeight * aspect;
        projection = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                0.05f, 4000.0f);
    } else {
        projection = glm::perspective(glm::radians(viewportFovDeg(camera)),
                                      aspect, 0.05f, 4000.0f);
    }
}

const std::vector<GizmoMark>& EditorApp::entityGizmoMarks()
{
    // Walks every entity, and the viewport is drawn while the gizmo is being
    // dragged -- so it follows the document's revision, like the outliner.
    // The isolated root is part of the key: entering or leaving the mode
    // changes which marks exist without touching the document.
    if (mGizmoMarksRevision != mState.document.revision ||
        mGizmoMarksIsolation != mState.isolation.root) {
        mGizmoMarksRevision = mState.document.revision;
        mGizmoMarksIsolation = mState.isolation.root;
        mGizmoMarks = collectGizmoMarks(mState.document);
        // A camera frustum or a light's radius belonging to an entity the mode
        // is hiding must go with it. These are ImGui overlay strokes, not
        // renderer geometry, so PreviewBridge's visibility filter never saw
        // them -- and a frustum drawn around an object being edited alone is
        // the level leaking back into the mode that exists to exclude it.
        if (mState.isolating()) {
            mGizmoMarks.erase(
                std::remove_if(mGizmoMarks.begin(), mGizmoMarks.end(),
                               [this](const GizmoMark& mark) {
                                   return !mState.isolation.contains(mark.id);
                               }),
                mGizmoMarks.end());
        }
    }
    return mGizmoMarks;
}

void EditorApp::drawEntityGizmos()
{
    if (!mShowEntityGizmos || materialMode() || mViewportW < 8.0f)
        return;
    glm::mat4 view, projection;
    cameraMatrices(mState.camera, mViewportW / mViewportH, view, projection);
    const glm::mat4 viewProjection = projection * view;

    GizmoOverlay overlay;
    overlay.viewProjection = &viewProjection;
    overlay.viewportOrigin = glm::vec2(mViewportX, mViewportY);
    overlay.viewportSize = glm::vec2(mViewportW, mViewportH);
    overlay.selected = &mState.selection;
    // The entity the Inspector is editing. It outranks the rest of a
    // multi-selection for the volume and label budgets, and wears a ring, so
    // "which of these eleven is the panel on the right about" has an answer on
    // screen instead of only in the panel's own header.
    if (const AuthorId* primary = mState.primary())
        overlay.primary = primary;
    overlay.hidden = &mState.hidden;
    overlay.locked = &mState.locked;
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
                12.0f, &mState.hidden, &mState.locked)) {
            hovered = mark->id;
            overlay.hovered = &hovered;
        }
    }
    // Under the marks, so the object and its handles read on top of it.
    if (mState.isolating() && mShowGrid) {
        const glm::vec3 eye = mState.camera.activeEye();
        drawSandboxGrid(ImGui::GetWindowDrawList(), viewProjection,
                        glm::vec2(mViewportX, mViewportY),
                        glm::vec2(mViewportW, mViewportH), eye,
                        mState.gridState.level, mState.grid.cell, 16);
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
            glm::vec2(mViewportX, mViewportY),
            glm::vec2(mViewportW, mViewportH), glm::vec2(mouse.x, mouse.y),
            12.0f, &mState.hidden, &mState.locked)) {
        selectAndReveal(mark->id, ImGui::GetIO().KeyShift);
        return;
    }

    // Everything under the cursor, nearest first. A repeated click at the same
    // spot walks down the list instead of selecting the nearest thing forever
    // -- Gregory §15.4.1.4, and the answer to a dense level that otherwise
    // leaves the author hiding whatever is in front and trying again.
    const std::vector<DocumentHit> hits = raycastDocumentAll(
        mState.document, mState.catalog, mouseRay(),
        [this](const AuthorId& id) {
            return !mState.isHidden(id) && !mState.isLocked(id) &&
                   (!mState.isolating() || mState.isolation.contains(id));
        });
    if (hits.empty()) {
        mPickCycle.reset();
        if (!ImGui::GetIO().KeyShift)
            mState.selection.clear();
        return;
    }

    // Resolved to the entity each hit MEANS -- the composed object it belongs
    // to -- before cycling, so the cycle walks distinct selectable things
    // rather than repeatedly offering the same parent from four of its parts.
    std::vector<AuthorId> targets;
    for (const DocumentHit& hit : hits) {
        const AuthorId target = resolvePick(hit.id);
        bool seen = false;
        for (const AuthorId& already : targets)
            seen = seen || already == target;
        if (!seen)
            targets.push_back(target);
    }

    const AuthorId picked =
        mPickCycle.next(targets, glm::vec2(mouse.x, mouse.y));
    if (picked.empty())
        return;
    selectAndReveal(picked, ImGui::GetIO().KeyShift);
    if (mPickCycle.count() > 1) {
        mStatus = std::to_string(mPickCycle.depth() + 1) + " of " +
                  std::to_string(mPickCycle.count()) +
                  " under the cursor -- click again for the next";
    }
}

// Rubber-band selection. Gregory §15.4.1.4: "Objects might be selected via a
// rubber-band box in the orthographic view or by ray cast style picking in the
// 3D view." Both here -- the band works in either projection, because it tests
// projected bounds against a screen rectangle and does not care how the
// projection was built.
//
// Only starts on empty space. A drag that begins on an entity is a gizmo drag
// or a click, and a band that stole those would make the viewport unusable.
void EditorApp::handleMarquee(const eng::FrameContext& f)
{
    (void)f;
    if (materialMode() || mViewportW < 8.0f)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;

    if (!mMarqueeDragging) {
        const bool canStart = mViewportHovered && !mFlying &&
                              mState.tool == Tool::Select &&
                              !ImGuizmo::IsUsingAny() && !mGizmoHovered &&
                              ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (!canStart)
            return;
        // Nothing under the cursor: this is a band, not a pick. Checked with
        // the same traversal the click uses, so the two can never disagree
        // about whether the author hit something.
        if (hoveredEntity().valid)
            return;
        mMarqueeDragging = true;
        mMarqueeStart = {mouse.x, mouse.y};
        return;
    }

    const selection::ScreenRect rect =
        selection::ScreenRect::fromCorners(mMarqueeStart, {mouse.x, mouse.y});

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // Drawn over the viewport image, like the entity marks: a band that
        // only appeared once released would be a gesture with no feedback.
        if (!rect.degenerate()) {
            ImDrawList* list = ImGui::GetWindowDrawList();
            const ImU32 line = ImGui::GetColorU32(ImGuiCol_CheckMark);
            list->AddRectFilled(ImVec2(rect.min.x, rect.min.y),
                                ImVec2(rect.max.x, rect.max.y),
                                (line & 0x00FFFFFF) | 0x22000000);
            list->AddRect(ImVec2(rect.min.x, rect.min.y),
                          ImVec2(rect.max.x, rect.max.y), line);
        }
        return;
    }

    mMarqueeDragging = false;
    if (rect.degenerate())
        return; // a click with a shaky hand, already handled as a pick

    std::vector<selection::Candidate> candidates;
    candidates.reserve(mState.document.entities.size());
    for (const Entity& entity : mState.document.entities) {
        if (mState.isHidden(entity.id) || mState.isLocked(entity.id))
            continue;
        if (mState.isolating() && !mState.isolation.contains(entity.id))
            continue;
        glm::vec3 localMin(-0.5f), localMax(0.5f);
        if (const KitPiece* piece = mState.catalog.find(entity.prefab))
            piece->localBoundsMeters(mState.catalog.scale(), localMin, localMax);
        selection::Candidate candidate;
        candidate.id = entity.id;
        transformedBounds(mState.document.worldTransform(entity.id), localMin,
                          localMax, candidate.boundsMin, candidate.boundsMax);
        candidates.push_back(std::move(candidate));
    }

    glm::mat4 view, projection;
    cameraMatrices(mState.camera, mViewportW / mViewportH, view, projection);
    // Alt demands the whole footprint inside the box. The default takes
    // anything the band crosses, which is what a quick sweep along a row of
    // props means; Alt is for boxing one room without catching the corridor.
    const selection::Fit fit = io.KeyAlt ? selection::Fit::Enclose
                                         : selection::Fit::Touch;
    std::vector<AuthorId> caught = selection::marquee(
        candidates, projection * view, glm::vec2(mViewportX, mViewportY),
        glm::vec2(mViewportW, mViewportH), rect, fit);

    // Resolved the same way a click is, so a band over a composed object
    // selects the object rather than forty of its parts.
    std::vector<AuthorId> resolved;
    for (const AuthorId& id : caught) {
        const AuthorId target = resolvePick(id);
        bool seen = false;
        for (const AuthorId& already : resolved)
            seen = seen || already == target;
        if (!seen)
            resolved.push_back(target);
    }

    if (!io.KeyShift)
        mState.selection.clear();
    for (const AuthorId& id : resolved)
        if (!mState.isSelected(id))
            mState.selection.push_back(id);

    mPickCycle.reset();
    mSelectionAnchor.clear();
    if (!mState.selection.empty())
        mOutlinerReveal = mState.selection.front();
    mStatus = std::to_string(resolved.size()) + " selected by band" +
              (fit == selection::Fit::Enclose ? " (fully enclosed)" : "");
}

// --- alignment aids ---------------------------------------------------------
//
// Gregory §15.4.1.7. The arithmetic is in ed::align and is checked headlessly;
// what lives here is resolving each entity's world bounds and turning the
// world-space moves that come back into the authored local transforms the file
// stores -- which is what keeps a parented entity where it is put.

void EditorApp::applyMoves(const std::vector<align::Move>& moves,
                           const std::string& label)
{
    std::vector<Command> parts;
    for (const align::Move& move : moves) {
        const Entity* entity = mState.document.find(move.id);
        if (!entity)
            continue;
        game::content::WorldTransform world =
            mState.document.worldTransform(move.id);
        if (glm::distance(world.position, move.position) < 1e-4f)
            continue; // already there; not worth an undo entry
        world.position = move.position;

        const XformAuthor before = entity->transform;
        XformAuthor after = before;
        if (entity->parent.empty()) {
            after.position = move.position;
        } else {
            after = game::content::localFromWorld(
                mState.document.worldTransform(entity->parent), world);
        }
        parts.push_back(makeSetTransform(move.id, before, after));
    }
    if (parts.empty()) {
        mStatus = label + ": already in place";
        return;
    }
    const std::size_t count = parts.size();
    runCommand(makeComposite(label, std::move(parts)));
    mStatus = label + ": " + std::to_string(count) + " moved";
}

// The selection's world bounds, in the form the align maths takes. Catalogue
// bounds rather than loaded meshes, for the reason raycastDocument uses them:
// an entity has to be alignable before its mesh is on disk.
static std::vector<align::Placement>
placementsOf(const game::content::SceneDocument& document,
             const game::content::KitCatalog& catalog,
             const std::vector<AuthorId>& ids)
{
    std::vector<align::Placement> placements;
    for (const AuthorId& id : ids) {
        const Entity* entity = document.find(id);
        if (!entity)
            continue;
        glm::vec3 localMin(-0.5f), localMax(0.5f);
        if (const KitPiece* piece = catalog.find(entity->prefab))
            piece->localBoundsMeters(catalog.scale(), localMin, localMax);
        align::Placement placement;
        placement.id = id;
        const game::content::WorldTransform world = document.worldTransform(id);
        placement.position = world.position;
        transformedBounds(world, localMin, localMax, placement.boundsMin,
                          placement.boundsMax);
        placements.push_back(std::move(placement));
    }
    return placements;
}

void EditorApp::alignSelection(align::Axis axis, align::Mode mode)
{
    const std::vector<align::Placement> placements =
        placementsOf(mState.document, mState.catalog, mState.selection);
    applyMoves(align::alignTo(placements, axis, mode), align::label(axis, mode));
}

void EditorApp::distributeSelection(align::Axis axis)
{
    const std::vector<align::Placement> placements =
        placementsOf(mState.document, mState.catalog, mState.selection);
    applyMoves(align::distribute(placements, axis),
               std::string("distribute ") + align::axisName(axis));
}

// "Snap to terrain" in the chapter's list. This engine has no terrain, so the
// same gesture runs against the geometry that does exist: cast down from each
// entity and rest its underside on whatever is below.
void EditorApp::dropSelectionToSurface()
{
    const std::vector<align::Placement> placements =
        placementsOf(mState.document, mState.catalog, mState.selection);

    std::vector<align::Drop> floors;
    for (const align::Placement& placement : placements) {
        Ray down;
        // Started just above the underside so the entity cannot hit itself,
        // which is what a ray from its centre would do.
        down.origin = {(placement.boundsMin.x + placement.boundsMax.x) * 0.5f,
                       placement.boundsMin.y + 0.01f,
                       (placement.boundsMin.z + placement.boundsMax.z) * 0.5f};
        down.dir = {0.0f, -1.0f, 0.0f};
        const DocumentHit hit = raycastDocument(
            mState.document, mState.catalog, down,
            [this, &placement](const AuthorId& id) {
                // Never itself, never anything else being dropped in the same
                // gesture -- otherwise a stack lands on its own falling parts.
                if (id == placement.id || mState.isSelected(id))
                    return false;
                return !mState.isHidden(id);
            });
        if (hit.valid)
            floors.push_back(align::Drop{placement.id, hit.boundsMax.y});
    }

    if (floors.empty()) {
        mStatus = "drop to surface: nothing underneath";
        return;
    }
    applyMoves(align::dropTo(placements, floors), "drop to surface");
}

// --- camera bookmarks and history -------------------------------------------

void EditorApp::setViewProjection(EditorCamera::Projection projection)
{
    if (mState.camera.projection() == projection)
        return;
    const bool entering =
        mState.camera.projection() == EditorCamera::Projection::Perspective &&
        projection != EditorCamera::Projection::Perspective;
    mState.camera.setProjection(projection);
    if (!entering)
        return;

    // Frame the selection if there is one, the level otherwise. The camera
    // cannot do this itself -- it has no document -- and an elevation that
    // opens on a corner of the scene is one the author has to hunt around in
    // before it is any use.
    glm::vec3 min, max;
    if (!boundsOf(mState.selection, min, max))
        return;
    mState.camera.setOrthoFocus((min + max) * 0.5f);

    // The span is taken across the two axes this elevation actually shows, and
    // padded: a plan view whose contents touch all four edges reads as cropped.
    const glm::vec3 size = max - min;
    float across = 0.0f;
    float up = 0.0f;
    switch (projection) {
    case EditorCamera::Projection::Top:
        across = size.x;
        up = size.z;
        break;
    case EditorCamera::Projection::Front:
        across = size.x;
        up = size.y;
        break;
    case EditorCamera::Projection::Side:
        across = size.z;
        up = size.y;
        break;
    case EditorCamera::Projection::Perspective:
        return;
    }
    const float aspect = mViewportH > 1.0f ? mViewportW / mViewportH : 1.6f;
    // Whichever axis is the binding constraint decides the span.
    const float wanted = std::max(up, across / std::max(aspect, 0.01f));
    // zoomOrtho is multiplicative and clamps, so the span is set by asking for
    // the ratio rather than by writing the field -- one place enforces the
    // limits.
    if (wanted > 0.0f)
        mState.camera.zoomOrtho(wanted * 1.15f / mState.camera.orthoHeight());
}

void EditorApp::pushCameraHistory()
{
    mCameraHistory.push(nav::poseOf(mState.camera));
}

void EditorApp::setBookmark(std::size_t slot)
{
    mBookmarks.set(slot, nav::poseOf(mState.camera));
    mStatus = "saved view " + std::to_string(slot + 1);
}

void EditorApp::jumpToBookmark(std::size_t slot)
{
    if (!mBookmarks.has(slot)) {
        mStatus = "view " + std::to_string(slot + 1) + " is empty -- " +
                  "Ctrl+Shift+" + std::to_string((slot + 1) % 10) + " saves it";
        return;
    }
    pushCameraHistory();
    nav::applyPose(mState.camera, mBookmarks.at(slot).pose);
    mStatus = "jumped to " + mBookmarks.at(slot).name;
}

void EditorApp::navigateHistory(bool forward)
{
    const nav::Pose current = nav::poseOf(mState.camera);
    nav::Pose target;
    const bool moved = forward ? mCameraHistory.forward(current, target)
                               : mCameraHistory.back(current, target);
    if (!moved) {
        mStatus = forward ? "nothing forward" : "nothing back";
        return;
    }
    nav::applyPose(mState.camera, target);
    mStatus = forward ? "forward" : "back";
}

// A viewport hit is a mesh; this is the entity that click means. The rule lives
// in PickTarget.h, where it can be tested without an editor.
game::content::AuthorId EditorApp::resolvePick(const AuthorId& hit) const
{
    return resolvePickTarget(mState.document, hit, mState.selection,
                             ImGui::GetIO().KeyAlt);
}

void EditorApp::finishGizmoDrag()
{
    if (!mGizmoDragging)
        return;
    mGizmoDragging = false;
    std::vector<Command> parts;
    for (const auto& [id, before] : mDragStart) {
        const Entity* entity = mState.document.find(id);
        if (!entity)
            continue;
        if (entity->transform.position == before.position &&
            entity->transform.rotationDegrees == before.rotationDegrees &&
            entity->transform.scale == before.scale)
            continue;
        parts.push_back(makeSetTransform(id, before, entity->transform));
    }
    if (!parts.empty()) {
        const char* label = mGizmoOperation == 1   ? "rotate selection"
                            : mGizmoOperation == 2 ? "scale selection"
                                                   : "move selection";
        // Document already holds final value, so applying this command is
        // idempotent and gives undo/redo one transaction for whole drag.
        runCommand(makeComposite(label, std::move(parts)));
    }
    mDragStart.clear();
}

void EditorApp::drawGizmo(const eng::FrameContext& f)
{
    mGizmoHovered = false;
    if (mGizmoOperation < 0 || mViewportW < 8.0f || mViewportH < 8.0f) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            finishGizmoDrag();
        return;
    }

    // A parent already carries every selected descendant through its hierarchy.
    // Manipulating both would apply the same group delta twice to the child.
    std::vector<AuthorId> transformSelection;
    transformSelection.reserve(mState.selection.size());
    for (const AuthorId& id : mState.selection) {
        const Entity* entity = mState.document.find(id);
        if (!entity || mState.isHidden(id) || mState.isLocked(id))
            continue;
        bool selectedAncestor = false;
        std::vector<AuthorId> seen;
        for (AuthorId parent = entity->parent; !parent.empty();) {
            if (std::find(seen.begin(), seen.end(), parent) != seen.end())
                break;
            seen.push_back(parent);
            if (mState.isSelected(parent)) {
                selectedAncestor = true;
                break;
            }
            const Entity* ancestor = mState.document.find(parent);
            parent = ancestor ? ancestor->parent : AuthorId{};
        }
        if (!selectedAncestor)
            transformSelection.push_back(id);
    }
    if (transformSelection.empty()) {
        // Selection can disappear through another editor action. Never leave a
        // dead transaction claiming ownership of every later shortcut.
        finishGizmoDrag();
        return;
    }
    const Entity* primary = mState.document.find(transformSelection.front());
    if (!primary)
        return;

    // Follows the viewport: ImGuizmo builds its own handle geometry from the
    // projection, and a perspective gizmo drawn over an orthographic view has
    // handles that do not line up with the axis they drag.
    ImGuizmo::SetOrthographic(mState.camera.orthographic());
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
    const bool haveBounds = boundsOf(transformSelection, boundsMin, boundsMax);
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
    const bool multi = transformSelection.size() > 1;
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
        for (const AuthorId& id : transformSelection)
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
                after.orientation =
                    rotating ? spin * was.orientation : was.orientation;
                after.scale = scaling ? was.scale * groupScale : was.scale;
                store(*entity, frame, after);
            }
        }
        mState.document.touch();
    }

    if (!gizmoUsing)
        finishGizmoDrag();

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
    // An elevation's rays are parallel, not a pinhole fan. Picking that used
    // the perspective builder in an orthographic view would miss by more the
    // further the cursor was from the centre of the viewport.
    const float orthoHeight = mState.camera.activeOrthoHeight();
    if (orthoHeight > 0.0f) {
        return orthoViewportRay({mouse.x, mouse.y}, {mViewportX, mViewportY},
                                {mViewportW, mViewportH},
                                mState.camera.activeEye(),
                                mState.camera.activeOrientation(), orthoHeight);
    }
    return viewportRay({mouse.x, mouse.y}, {mViewportX, mViewportY},
                       {mViewportW, mViewportH}, mState.camera.activeEye(),
                       mState.camera.activeOrientation(),
                       glm::radians(mState.camera.activeFovDeg()));
}

DocumentHit EditorApp::hoveredEntity(bool ignoreStroke) const
{
    if (mViewportW < 8.0f)
        return DocumentHit{};
    const Ray ray = mouseRay();
    return raycastDocument(mState.document, mState.catalog, ray,
                           [&](const AuthorId& id) {
                               // Hidden is not there; locked is there and not
                               // targetable. Locking the floor is how a room
                               // gets dressed without picking it forty times --
                               // and, now, without every prop in the stroke
                               // landing on the last prop of the stroke.
                               if (mState.isHidden(id) || mState.isLocked(id))
                                   return false;
                               if (!mPreview->entityVisible(id))
                                   return false;
                               if (ignoreStroke)
                                   for (const AuthorId& painted : mStrokeIds)
                                       if (painted == id)
                                           return false;
                               return true;
                           });
}

Placement EditorApp::hoveredPlacement() const
{
    if (mState.brush.empty() || mViewportW < 8.0f)
        return Placement{};

    PlacementQuery query;
    query.ray = mouseRay();
    query.workPlaneLevel = mState.gridState.level;
    query.snapXZ = mState.gridState.snap;
    query.step = mState.gridState.step();
    query.forceWorkPlane = ImGui::GetIO().KeyCtrl;
    query.fallbackDistance = kGhostFallbackDistance;
    // The stroke's own pieces are excluded: a row of floor tiles painted in one
    // drag must all land on the same surface, not climb the one before it.
    query.surface = hoveredEntity(true);
    // A kit piece carries its own pivot; the two free-geometry brushes have to
    // be told, or a centre-authored mesh is placed half through the floor.
    if (mPreview) {
        if (mState.brush.kind == Brush::Kind::Mesh)
            query.baseOffset =
                mPreview->meshBaseOffset(mState.brush.meshPath, 1.0f);
        else if (mState.brush.kind == Brush::Kind::Primitive)
            query.baseOffset =
                mPreview->primitiveBaseOffset(mState.brush.primitive);
    }

    return resolvePlacement(mState.grid, mState.catalog, mState.brush, query);
}

void EditorApp::placeAt(const Placement& placement)
{
    if (!placement.valid)
        return;

    const KitPiece* piece = mState.brush.kind == Brush::Kind::Piece
                                ? mState.catalog.find(mState.brush.prefab)
                                : nullptr;
    const bool grids = piece && socketUsesGrid(piece->socket);

    // One entity per slot per stroke: a stroke places every frame the button is
    // held, so without this a click that lasts a second drops sixty props.
    // Grid pieces key on their cell; anything free keys on where it landed.
    const std::string slot =
        grids ? gridPaintSlot(placement.cell)
              : freePaintSlot(placement.transform.position,
                              mState.gridState.snap ? mState.gridState.step()
                                                    : kFreePaintSpacing);
    if (!mStrokeSlots.insert(slot).second)
        return;

    Entity entity;
    if (mState.brush.kind == Brush::Kind::Gameplay) {
        entity = makeGameplayEntity(mState.brush.gameplay, placement.transform);
    }
    else if (mState.brush.kind == Brush::Kind::Particles) {
        entity.id = mState.document.allocateId(mState.brush.effect + "_fx");
        entity.name = mState.brush.effect;
        entity.transform = placement.transform;
        game::content::ParticleAuthor particles;
        particles.effect = mState.brush.effect;
        particles.playing = true;
        particles.scale = mParticlePreviewScale;
        entity.particles = std::move(particles);
    }
    // The two non-kit geometry brushes. They place exactly as a prop does --
    // free position, no cell -- because neither carries a socket to snap with.
    else if (mState.brush.kind == Brush::Kind::Mesh) {
        const std::string stem =
            std::filesystem::path(mState.brush.meshPath).stem().string();
        entity.id = mState.document.allocateId(stem.empty() ? "mesh" : stem);
        entity.name = entity.id;
        entity.transform = placement.transform;
        entity.mesh = game::content::MeshAuthor{mState.brush.meshPath, 1.0f};
        if (const MeshAsset* asset = meshCatalog().find(mState.brush.meshPath))
            entity.material = asset->material;
    }
    else if (mState.brush.kind == Brush::Kind::Primitive) {
        entity.id = mState.document.allocateId(
            eng::ecs::primitiveKindName(mState.brush.primitive.kind));
        entity.name = entity.id;
        entity.transform = placement.transform;
        entity.primitive = mState.brush.primitive;
    }
    else {
        entity.id = mState.document.allocateId(mState.brush.prefab);
        entity.name = entity.id;
        entity.prefab = mState.brush.prefab;
        entity.transform = placement.transform;
        if (grids)
            entity.cell = placement.cell;
        // Components the piece declares it cannot work without, through the
        // same registry the inspector's Add Component uses. This is what stops
        // a portal membrane placed from the Catalog being a dead rectangle
        // while the identical-looking gameplay entry produces a live one.
        if (piece)
            for (const std::string& component : piece->components)
                if (const ComponentType* type = findComponentType(component))
                    type->add(entity, componentDefaults());
        // A compound piece arrives with its parts in the document rather than
        // baked in by the cooker. What you place is then what the hierarchy
        // shows: the boss AND its sword, each selectable, movable and
        // materialled on its own. Before this the sword was visible in the
        // viewport, absent from the tree, and unreachable except through a
        // context-menu command nobody finds.
        if (mPlaceUnpacksParts && hasPackedAttachments(mState.catalog, entity))
            entity.unpackedAttachments = true;
    }

    stampLayer(entity);
    // Applied immediately so the next hover sees it; the command is recorded
    // and pushed as one composite when the drag ends.
    mState.document.add(entity);
    mStrokeIds.push_back(entity.id);
    adoptIntoIsolation(entity);
    mStrokeParts.push_back(makeCreateEntity(entity));
    // The parts go down the same way, in the same stroke, so one undo takes
    // the whole object back. Ids come from the live document, which already
    // holds everything this stroke has laid.
    if (entity.unpackedAttachments) {
        for (const Entity& part :
             buildAttachmentEntities(mState.catalog, mState.document, entity)) {
            mStrokeIds.push_back(part.id);
            mStrokeParts.push_back(makeCreateEntity(part));
        }
    }
}

void EditorApp::eraseAt(const DocumentHit& hit)
{
    if (!hit.valid)
        return;
    // The entity's id is its own slot: crossing it twice in one drag is one
    // removal, and the document has already lost it by the second crossing.
    if (!mStrokeSlots.insert(hit.id).second)
        return;
    if (!mState.document.find(hit.id))
        return;

    mStrokeParts.push_back(makeDeleteEntity(mState.document, hit.id));
    mState.document.remove(hit.id);
    // A removed entity cannot stay selected: the gizmo would draw against a
    // transform nothing owns.
    for (std::size_t i = 0; i < mState.selection.size(); ++i) {
        if (mState.selection[i] == hit.id) {
            mState.selection.erase(mState.selection.begin() +
                                   std::ptrdiff_t(i));
            break;
        }
    }
}

void EditorApp::finishStroke()
{
    const Stroke stroke = mStroke;
    mStroke = Stroke::None;
    mStrokeSlots.clear();
    if (mStrokeParts.empty()) {
        mStrokeIds.clear();
        return;
    }

    // The stroke was applied straight to the document so the ghost had
    // something to follow. Roll that back and let the command apply it
    // properly: the end state is identical, and now the entry has a real apply
    // for redo instead of a no-op that would lose the pieces.
    const std::size_t count = mStrokeParts.size();
    std::string label;
    if (stroke == Stroke::Erase) {
        label = "erase " + std::to_string(count) +
                (count == 1 ? " entity" : " entities");
        // Put back what the live stroke took, newest first, so the composite
        // below is what actually performs the deletion.
        for (std::size_t i = count; i-- > 0;)
            mStrokeParts[i].revert(mState.document);
    }
    else {
        const std::string subject =
            mState.brush.kind == Brush::Kind::Gameplay
                ? std::string(gameplayName(mState.brush.gameplay))
            : mState.brush.kind == Brush::Kind::Particles ? mState.brush.effect
                                                          : mState.brush.prefab;
        label = "place " + std::to_string(count) + " x " + subject;
        for (const AuthorId& id : mStrokeIds)
            mState.document.remove(id);
    }

    runCommand(makeComposite(label, std::move(mStrokeParts)));
    mStrokeParts.clear();
    mStrokeIds.clear();
    mPreview->invalidate();
}

void EditorApp::pickBrushFrom(const DocumentHit& hit)
{
    if (!hit.valid)
        return;
    const Entity* entity = mState.document.find(hit.id);
    if (!entity)
        return;

    if (entity->particles && !entity->particles->effect.empty()) {
        mState.brush.kind = Brush::Kind::Particles;
        mState.brush.effect = entity->particles->effect;
        mState.brush.yawQuarters =
            int(std::lround(entity->transform.rotationDegrees.y / 90.0f));
        mState.brush.rotate(0);
        mStatus = "particle brush: " + mState.brush.effect;
        return;
    }
    if (!entity->prefab.empty() && mState.catalog.find(entity->prefab)) {
        mState.brush.kind = Brush::Kind::Piece;
        mState.brush.prefab = entity->prefab;
        // The rotation comes with it: picking up a wall and laying more of them
        // means laying them the same way round.
        if (entity->cell)
            mState.brush.yawQuarters = entity->cell->yawQuarters;
        mStatus = "brush: " + entity->prefab;
        return;
    }
    mStatus = entity->id + " has no kit piece to pick up";
}

void EditorApp::drawPlacementGhost()
{
    if (mState.tool != Tool::Place || !mViewportHovered || mFlying) {
        mPreview->hidePlacementGhost();
        return;
    }

    const Placement placement = hoveredPlacement();
    if (!placement.valid) {
        mPreview->hidePlacementGhost();
        return;
    }

    // Meshless gameplay gets a unit box. Particles get both a tight authoring
    // box and the real renderer effect, because spread and motion cannot be
    // judged from a transform icon.
    glm::vec3 localMin(-0.5f), localMax(0.5f);
    // A brush that draws its own geometry needs no outline. The wire box was
    // the only readable thing about a ghost while the ghost itself was drawn at
    // the wrong scale; now that the mesh lands at the size it will be placed
    // at, the box is a second, coarser answer to a question already answered --
    // and on a compound piece it was a crate around a boss.
    bool wireBox = true;
    const KitPiece* piece = mState.brush.kind == Brush::Kind::Piece
                                ? mState.catalog.find(mState.brush.prefab)
                                : nullptr;
    if (piece) {
        mPreview->showPlacementGhost(mState.catalog, *piece,
                                     placement.transform);
        piece->localBoundsMeters(mState.catalog.scale(), localMin, localMax);
        // The catalogue's size is authored per piece and a group has none, so
        // the rig's own union is the better answer when it has one.
        glm::vec3 rigMin, rigMax;
        if (mPreview->ghostBounds(rigMin, rigMax)) {
            localMin = rigMin;
            localMax = rigMax;
            wireBox = false;
        }
    }
    else if (mState.brush.kind == Brush::Kind::Mesh) {
        mPreview->showMeshPlacementGhost(mState.brush.meshPath,
                                         placement.transform, 1.0f);
        if (!mPreview->ghostBounds(localMin, localMax)) {
            localMin = glm::vec3(-0.5f);
            localMax = glm::vec3(0.5f);
        }
        else {
            wireBox = false;
        }
    }
    else if (mState.brush.kind == Brush::Kind::Primitive) {
        mPreview->showPrimitivePlacementGhost(mState.brush.primitive,
                                              placement.transform);
        if (!mPreview->ghostBounds(localMin, localMax)) {
            localMin = glm::vec3(-0.5f);
            localMax = glm::vec3(0.5f);
        }
        else {
            wireBox = false;
        }
    }
    else if (mState.brush.kind == Brush::Kind::Particles) {
        float repeatSeconds = 0.0f;
        // Fit the guide to emitter volumes/positions. Point effects still get a
        // small visible target, independent of their billboard art.
        localMin = glm::vec3(-0.15f);
        localMax = glm::vec3(0.15f);
        for (const eng::ParticleEffectDesc& desc : mParticles.descs()) {
            if (desc.name != mState.brush.effect)
                continue;
            repeatSeconds = particlePreviewPeriod(desc);
            const glm::vec3 artHalf(
                std::max({desc.baseWidth, desc.baseHeight, 0.30f}) * 0.5f *
                mParticlePreviewScale);
            localMin = -artHalf;
            localMax = artHalf;
            for (const eng::ParticleEmitterDesc& emitter : desc.emitters) {
                const glm::vec3 half =
                    emitter.shape == eng::ParticleEmitterShape::Box
                        ? emitter.boxSize * 0.5f
                        : glm::vec3(0.05f);
                localMin = glm::min(localMin, emitter.position - half);
                localMax = glm::max(localMax, emitter.position + half);
            }
            break;
        }
        mPreview->showParticlePlacementGhost(
            mState.brush.effect, placement.transform, mParticlePreviewScale,
            repeatSeconds);
    }
    else {
        mPreview->hidePlacementGhost();
    }

    // The ghost is a wire box in screen space rather than a translucent mesh:
    // it needs no material, cannot be picked, and reads clearly against any
    // geometry behind it.
    const glm::mat4 worldMatrix = authorTransformMatrix(placement.transform);

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
    // Green when the height came from the work plane, amber when it came from
    // geometry under the cursor. The two land in different places and used to
    // be indistinguishable until after the click.
    const ImU32 colour = placement.onSurface ? IM_COL32(250, 200, 110, 220)
                                             : IM_COL32(120, 220, 160, 200);
    // Only for the brushes with nothing to draw -- a gameplay entity, a
    // particle effect, a mesh that would not load. Where there IS a mesh, it is
    // now the ghost, and boxing it as well is noise.
    if (wireBox) {
        for (const auto& edge : kEdges) {
            ImVec2 a, b;
            if (project(corners[edge[0]], a) && project(corners[edge[1]], b))
                draw->AddLine(a, b, colour, 1.5f);
        }
    }

    // The drop point, always: a small mark at the brush's own origin, in the
    // same green/amber that says where the height came from. With the box gone
    // this is what carries that distinction -- and on a mesh ghost it is also
    // the only thing that says which point of it the cursor is holding.
    const glm::vec3 base = placement.transform.position;
    glm::vec2 top;
    const bool haveBase =
        projectToViewport(base, viewProjection, {mViewportX, mViewportY},
                          {mViewportW, mViewportH}, top);
    if (haveBase) {
        const float r = 5.0f * mAppliedUiScale;
        const ImVec2 centre(top.x, top.y);
        const ImVec2 diamond[4] = {ImVec2(centre.x, centre.y - r),
                                   ImVec2(centre.x + r, centre.y),
                                   ImVec2(centre.x, centre.y + r),
                                   ImVec2(centre.x - r, centre.y)};
        draw->AddPolyline(diamond, 4, colour, ImDrawFlags_Closed, 1.5f);
    }

    // A stalk down to the work plane whenever the piece is off it, so the
    // author can see how high the thing is floating rather than inferring it
    // from perspective alone.
    if (haveBase && std::fabs(base.y - mState.gridState.level) > 1e-3f) {
        glm::vec2 foot;
        if (projectToViewport({base.x, mState.gridState.level, base.z},
                              viewProjection, {mViewportX, mViewportY},
                              {mViewportW, mViewportH}, foot)) {
            draw->AddLine(ImVec2(top.x, top.y), ImVec2(foot.x, foot.y),
                          IM_COL32(250, 200, 110, 110), 1.0f);
        }
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

    // Follows the viewport: ImGuizmo builds its own handle geometry from the
    // projection, and a perspective gizmo drawn over an orthographic view has
    // handles that do not line up with the axis they drag.
    ImGuizmo::SetOrthographic(mState.camera.orthographic());
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
    options.groupRepeats = mOutlinerGroupRepeats;
    // While isolated the panel is the object's own tree, not the level with one
    // object visible in it. That is the difference between a mode and a filter.
    options.root = mState.isolating() ? mState.isolation.root : AuthorId{};
    // Grouping walks and sorts every entity, and this panel is open while the
    // gizmo is dragged -- so it is rebuilt on a document revision or an option
    // change, never per frame.
    if (mOutlinerRevision != mState.document.revision ||
        options.filter != mOutlinerOptions.filter ||
        options.showGeometry != mOutlinerOptions.showGeometry ||
        options.groupRepeats != mOutlinerOptions.groupRepeats ||
        options.root != mOutlinerOptions.root) {
        mOutlinerRevision = mState.document.revision;
        mOutlinerOptions = options;
        mOutliner = buildOutliner(mState.document, mState.catalog, options);
    }
    return mOutliner;
}

void EditorApp::selectGroup(const OutlinerGroup& group, SelectMode mode)
{
    const std::vector<AuthorId> ids = groupIds(group);
    const bool allSelected =
        std::all_of(ids.begin(), ids.end(), [this](const AuthorId& id) {
            return mState.isSelected(id);
        });

    if (mode == SelectMode::Replace)
        mState.selection.clear();
    if (mode == SelectMode::Toggle && allSelected) {
        mState.selection.erase(
            std::remove_if(mState.selection.begin(), mState.selection.end(),
                           [&ids](const AuthorId& selected) {
                               return std::find(ids.begin(), ids.end(),
                                                selected) != ids.end();
                           }),
            mState.selection.end());
    }
    else {
        // Shift adds an aggregate. A range through a collapsed prefab group has
        // no honest row-by-row meaning; treating the group as one unit does.
        for (const AuthorId& id : ids)
            if (!mState.isSelected(id))
                mState.selection.push_back(id);
    }
    mSelectionAnchor.clear();
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
    const WorldTransform frame = parent.empty()
                                     ? WorldTransform{}
                                     : mState.document.worldTransform(parent);
    Entity updated = *source;
    updated.parent = parent;
    updated.transform = localFromWorld(frame, world);
    // A cell placement is addressed in the grid's frame, not a parent's, so a
    // parented piece is no longer grid-constrained. Keeping the cell would let
    // the cooker and the viewport disagree about where the piece is.
    if (!parent.empty())
        updated.cell.reset();

    out = makeEditEntity(parent.empty() ? "detach " + child
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
    mStatus =
        parent.empty() ? child + " detached" : child + " parented to " + parent;
}

void EditorApp::reparentSelection(const AuthorId& dragged,
                                  const AuthorId& parent)
{
    if (!mState.isSelected(dragged) || mState.selection.size() < 2) {
        reparentEntity(dragged, parent);
        return;
    }
    if (!parent.empty() && mState.isSelected(parent)) {
        mStatus = "cannot parent a selection inside itself";
        return;
    }

    std::vector<Command> parts;
    for (const AuthorId& id : mState.selection) {
        // Move selected roots only. Moving a selected parent and its selected
        // child separately would flatten their relationship at the target.
        bool ancestorSelected = false;
        const Entity* entity = mState.document.find(id);
        std::unordered_set<AuthorId> visited;
        while (entity && !entity->parent.empty() &&
               visited.insert(entity->parent).second) {
            if (mState.isSelected(entity->parent)) {
                ancestorSelected = true;
                break;
            }
            entity = mState.document.find(entity->parent);
        }
        if (ancestorSelected)
            continue;

        Command command;
        if (buildReparent(id, parent, command))
            parts.push_back(std::move(command));
    }
    if (parts.empty())
        return;

    const std::size_t moved = parts.size();
    runCommand(makeComposite(
        parent.empty() ? "detach " + std::to_string(moved)
                       : "parent " + std::to_string(moved) + " to " + parent,
        std::move(parts)));
    mPreview->invalidate();
    mStatus = parent.empty() ? std::to_string(moved) + " detached"
                             : std::to_string(moved) + " parented to " + parent;
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
    std::vector<AuthorId> keep =
        withDescendants(mState.document, mState.selection);
    // Preserve ancestor context too. Isolating one candle must not make its
    // chandelier root disappear from both viewport and hierarchy.
    for (const AuthorId& selected : mState.selection) {
        const Entity* entity = mState.document.find(selected);
        std::unordered_set<AuthorId> visited;
        while (entity && !entity->parent.empty() &&
               visited.insert(entity->parent).second) {
            if (std::find(keep.begin(), keep.end(), entity->parent) ==
                keep.end())
                keep.push_back(entity->parent);
            entity = mState.document.find(entity->parent);
        }
    }

    mState.hidden.clear();
    for (const Entity& entity : mState.document.entities) {
        if (std::find(keep.begin(), keep.end(), entity.id) == keep.end())
            mState.hidden.push_back(entity.id);
    }
    mPreview->invalidate();
    mStatus = "isolated " + std::to_string(keep.size()) + " of " +
              std::to_string(mState.document.entities.size());
}

void EditorApp::requestPanelFocus(const char* name)
{
    if (!name || !*name)
        return;
    // The bottom panel is not part of the dockspace, so bringing one of its
    // tabs forward means opening it rather than focusing a window.
    if (std::strcmp(name, "console") == 0 ||
        std::strcmp(name, "output") == 0) {
        mBottom.show(BottomTab::Output);
        return;
    }
    if (std::strcmp(name, "issues") == 0) {
        mBottom.show(BottomTab::Problems);
        return;
    }
    if (std::strcmp(name, "timeline") == 0) {
        mBottom.show(BottomTab::Timeline);
        return;
    }
    mFocusPanel = name;
    mFocusPanelFrames = 3;
}

// Brings a docked panel forward, for the frames after startup during which a
// restored layout is still re-selecting its own saved tabs. Verification hook
// (RAVEN_EDITOR_PANEL); does nothing in an ordinary session.
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
namespace {
// Kept beside the two canvases that load it rather than read from game.toml:
// the editor has no game config, and a panel that silently fell back to a
// different font than the game uses would be lying about the layout it shows.
constexpr const char* kUiFont = "antiquity.toml";
} // namespace

void EditorApp::drawUiStage()
{
    focusPanelIfRequested("ui");
    if (!ImGui::Begin(workspace_window::kViewport2D, nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }
    // The other half of the tab/switcher agreement -- see drawViewport.
    if (mScreen != MainScreen::Screen2D)
        mScreen = MainScreen::Screen2D;
    if (!mUiHudReady)
        // The game's own face, so the preview and the game agree about what
        // text costs: a layout that fits in one font and not the other is
        // exactly the failure this panel exists to catch. Named once here and
        // in game.toml's [ui] block; a project that ships its own overrides it
        // through the same mount.
        mUiHudReady = mUiHud.initialise(kUiFont);
    if (!mUiHudReady) {
        ImGui::TextColored(kUiDanger, "the HUD font atlas did not load");
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
    ImGui::Checkbox("Show safe-area guide", &mUiStage.showSafeArea);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(54.0f);
    ImGui::SliderInt("##safe-area-percent", &mUiStage.safeAreaPercent, 0, 15,
                     "%d%%");
    ImGui::SameLine();
    ImGui::Checkbox("grid", &mUiStage.showGrid);

    // --- the canvas -------------------------------------------------------
    // Drawn before the controls and given every pixel that is left: this is a
    // viewport, and a viewport that a row of sliders can squeeze to nothing is
    // a property sheet with a picture on it. The controls sit under it in a
    // fixed strip, which also means they never move as the panel resizes.
    const ImVec2 room = ImGui::GetContentRegionAvail();
    const float controlStrip = room.x < 520.0f ? 250.0f : 166.0f;
    const ImVec2 available(room.x, std::max(room.y - controlStrip, 48.0f));
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
    ImGui::SetNextWindowContentSize(
        ImVec2(std::max(extent.x, available.x - 2.0f),
               std::max(extent.y, available.y - 2.0f)));
    ImGui::BeginChild("##ui_canvas", available, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    // Centred when it fits; scrollable at a truthful integer 1x when it does
    // not. Silent clipping made small panel sizes look like HUD failures.
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 origin(
        cursor.x + std::max((available.x - extent.x) * 0.5f, 0.0f),
        cursor.y + std::max((available.y - extent.y) * 0.5f, 0.0f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 childPos = ImGui::GetWindowPos();
    const ImVec2 childSize = ImGui::GetWindowSize();
    draw->PushClipRect(
        childPos, ImVec2(childPos.x + childSize.x, childPos.y + childSize.y),
        true);
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
            draw->AddLine(
                ImVec2(origin.x + float(x * scale), origin.y),
                ImVec2(origin.x + float(x * scale), origin.y + extent.y), line);
        for (int y = 16; y < virtualSize.y; y += 16)
            draw->AddLine(
                ImVec2(origin.x, origin.y + float(y * scale)),
                ImVec2(origin.x + extent.x, origin.y + float(y * scale)), line);
    }

    // The real HUD, on the real canvas, into this panel's draw list.
    const float safeFraction = float(mUiStage.safeAreaPercent) / 100.0f;
    const eng::ui::Insets safeArea{
        int(std::lround(float(virtualSize.x) * safeFraction)),
        int(std::lround(float(virtualSize.y) * safeFraction)),
        int(std::lround(float(virtualSize.x) * safeFraction)),
        int(std::lround(float(virtualSize.y) * safeFraction))};
    mUiHud.drawInto(hudSnapshotFrom(mUiStage), hudTooltipFrom(mUiStage),
                    mUiStage.dt, {origin.x, origin.y}, virtualSize, scale, draw,
                    safeArea);

    // --- the authored screens ---------------------------------------------
    //
    // Drawn over the HUD preview, because that is the truth of it: a screen
    // opens on top of the HUD in the game too. Everything below edits the
    // document, so this panel is now an editor and not only a preview.
    if (!mUiSceneCanvasReady)
        mUiSceneCanvasReady = mUiSceneCanvas.initialise(kUiFont);
    if (mUiSceneCanvasReady) {
        mUiSceneEditor.rebuild(mState.document, virtualSize, mState.assetRoot);
        mUiSceneCanvas.beginTarget({origin.x, origin.y}, virtualSize, scale,
                                   draw);
        mUiSceneEditor.paint(mUiSceneCanvas);

        // Pointer, in virtual pixels. The inverse of the canvas mapping, so a
        // click lands on the box the author sees under the cursor at any scale.
        const ImVec2 mouse = ImGui::GetMousePos();
        const glm::ivec2 point{
            int(std::floor((mouse.x - origin.x) / float(scale))),
            int(std::floor((mouse.y - origin.y) / float(scale)))};
        const bool inside = ImGui::IsWindowHovered() && point.x >= 0 &&
                            point.y >= 0 && point.x < virtualSize.x &&
                            point.y < virtualSize.y;

        // The selection's handles. A constant *screen* reach, so a handle is as
        // easy to grab zoomed out as zoomed in.
        const int grab = std::max(1, 4 / std::max(scale, 1));
        const AuthorId selected =
            mState.selection.size() == 1 ? mState.selection.front() : AuthorId{};
        eng::ui::UiRect selectedBounds;
        const bool haveSelection =
            !selected.empty() && mUiSceneEditor.boundsOf(selected, selectedBounds);

        if (haveSelection) {
            const ImVec2 a(origin.x + float(selectedBounds.position.x * scale),
                           origin.y + float(selectedBounds.position.y * scale));
            const ImVec2 b(a.x + float(selectedBounds.size.x * scale),
                           a.y + float(selectedBounds.size.y * scale));
            draw->AddRect(a, b, IM_COL32(239, 172, 240, 220));
            const float chip = std::max(3.0f, float(scale) * 2.0f);
            const float xs[3] = {a.x, (a.x + b.x) * 0.5f, b.x};
            const float ys[3] = {a.y, (a.y + b.y) * 0.5f, b.y};
            for (int iy = 0; iy < 3; ++iy)
                for (int ix = 0; ix < 3; ++ix) {
                    if (ix == 1 && iy == 1)
                        continue; // the middle is the body, not a handle
                    draw->AddRectFilled(
                        ImVec2(xs[ix] - chip, ys[iy] - chip),
                        ImVec2(xs[ix] + chip, ys[iy] + chip),
                        IM_COL32(239, 172, 240, 235));
                }
        }

        if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // A grabbed handle keeps the current selection; a click anywhere
            // else re-picks. Re-picking on a handle grab is what makes a
            // resize occasionally select the panel underneath instead.
            const UiSceneEditor::Handle grabbed =
                haveSelection
                    ? UiSceneEditor::handleAt(selectedBounds, point, grab)
                    : UiSceneEditor::Handle::None;
            if (grabbed != UiSceneEditor::Handle::None) {
                mUiDragHandle = grabbed;
                mUiDragLast = point;
                mUiDragChanged = false;
                mUiDragId = selected;
                if (const Entity* start = mState.document.find(selected))
                    mUiDragBefore = *start;
            } else {
                const AuthorId hit = mUiSceneEditor.pick(point);
                mState.selection.clear();
                if (!hit.empty())
                    mState.selection.push_back(hit);
                mSelectionAnchor = hit;
                mUiDragHandle = hit.empty() ? UiSceneEditor::Handle::None
                                            : UiSceneEditor::Handle::Body;
                mUiDragLast = point;
                mUiDragChanged = false;
                mUiDragId = hit;
                if (const Entity* start = mState.document.find(hit))
                    mUiDragBefore = *start;
            }
        }

        if (mUiDragHandle != UiSceneEditor::Handle::None &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const glm::ivec2 step = point - mUiDragLast;
            if (step.x != 0 || step.y != 0) {
                // Mutated directly for live feedback. The command system is
                // still the only thing that *records* the change: on release the
                // document is put back and the whole drag replayed as one edit,
                // so undo sees one entry and the redo stack stays truthful.
                if (Entity* entity = mState.document.find(mUiDragId)) {
                    if (UiSceneEditor::applyDrag(*entity, mUiDragHandle, step))
                        mUiDragChanged = true;
                }
                mUiDragLast = point;
            }
        }
        if (mUiDragHandle != UiSceneEditor::Handle::None &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // One undo entry per drag, not per frame of it: a mouse move that
            // crossed forty pixels must not cost forty presses of ctrl-Z.
            if (mUiDragChanged) {
                if (Entity* entity = mState.document.find(mUiDragId)) {
                    Entity after = *entity;
                    *entity = mUiDragBefore;
                    runCommand(makeEditEntity("move UI element", mUiDragId,
                                              mUiDragBefore, std::move(after)));
                }
            }
            mUiDragHandle = UiSceneEditor::Handle::None;
            mUiDragChanged = false;
        }
    }

    if (mUiStage.showSafeArea) {
        // A console HUD that ignores the safe area is legible on a monitor and
        // cropped on a television, and the crop is not something the developer
        // ever sees.
        const ImVec2 a(origin.x + extent.x * safeFraction,
                       origin.y + extent.y * safeFraction);
        const ImVec2 b(origin.x + extent.x * (1.0f - safeFraction),
                       origin.y + extent.y * (1.0f - safeFraction));
        draw->AddRect(a, b, IM_COL32(198, 58, 64, 132));
    }
    // The frame last, so it is never drawn over.
    draw->AddRect(origin, ImVec2(origin.x + extent.x, origin.y + extent.y),
                  IM_COL32(120, 140, 160, 160));
    draw->PopClipRect();
    ImGui::Dummy(ImVec2(std::max(extent.x, available.x - 2.0f),
                        std::max(extent.y, available.y - 2.0f)));
    ImGui::EndChild();

    ImGui::TextDisabled("%d x %d virtual   x%d   %.0f x %.0f px", virtualSize.x,
                        virtualSize.y, scale, double(extent.x),
                        double(extent.y));

    // --- the state --------------------------------------------------------
    // Two columns of sliders in the strip below, because the failures worth
    // previewing are combinations: a long weapon name *and* three statuses
    // *and* a tooltip, at 320x240.
    if (extent.x > available.x || extent.y > available.y) {
        ImGui::SameLine();
        ImGui::TextColored(kUiWarning, "scroll to inspect the full canvas");
    }
    const int stateColumns = ImGui::GetContentRegionAvail().x >= 520.0f ? 2 : 1;
    if (ImGui::BeginTable("##uistate", stateColumns,
                          ImGuiTableFlags_SizingStretchSame)) {
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
        ImGui::SliderFloat("poise", &mUiStage.poise, 0.0f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(-leftLabels);
        ImGui::SliderInt("statuses", &mUiStage.statusCount, 0,
                         game::HudSnapshot::kMaxStatuses);

        const float rightLabels = ImGui::CalcTextSize("discipline").x + 12.0f;
        ImGui::TableNextColumn();
        char weapon[64];
        std::snprintf(weapon, sizeof(weapon), "%s",
                      mUiStage.weaponName.c_str());
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
            char body[128];
            std::snprintf(body, sizeof(body), "%s",
                          mUiStage.tooltipBody.c_str());
            ImGui::SetNextItemWidth(-rightLabels);
            if (ImGui::InputText("body", body, sizeof(body)))
                mUiStage.tooltipBody = body;
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void EditorApp::drawOutliner()
{
    focusPanelIfRequested("outliner");
    if (!ImGui::Begin(workspace_window::kSceneTree, nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }

    const bool focusSearch =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F);
    if (focusSearch)
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##outlinerfilter", "search  (has: kind:)",
                             mOutlinerFilter, sizeof(mOutlinerFilter));
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape))
        mOutlinerFilter[0] = '\0';
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("free text matches name, id, kind and prefab\n"
                          "has:collider   entities carrying that component\n"
                          "kind:enemy     entities of exactly that kind\n"
                          "terms are AND-ed: has:trigger kind:wall");

    if (ImGui::Button("+ Create"))
        ImGui::OpenPopup("##hierarchy_create");
    if (ImGui::BeginPopup("##hierarchy_create")) {
        if (ImGui::MenuItem("Empty group"))
            addGameplayEntity(Gameplay::Group);
        ImGui::Separator();
        for (const Gameplay kind : paintableGameplay())
            if (ImGui::MenuItem(gameplayName(kind)))
                addGameplayEntity(kind);
        ImGui::Separator();
        if (ImGui::MenuItem("Directional light"))
            addGameplayEntity(Gameplay::DirectionalLight);
        ImGui::EndPopup();
    }
    ui::sameLineIfItFits(ui::buttonWidth("Hierarchy"));
    // What a row in this panel MEANS, switched here.
    //
    // Grouped collapses repeats: a blockout's hundred and forty-six identical
    // walls are one row, which is the only way a dense level is readable. The
    // cost is that a row then means two different things -- an object and its
    // parts, or a pile of unrelated entities -- and the second is destructive
    // to mistake for the first.
    //
    // Hierarchy is the document's own structure: one row, one entity, children
    // under their parent, nothing merged and nothing invented. It is the answer
    // to "what is parented to what", and until it existed that question had no
    // surface in the editor for a scene whose entities are mostly flat.
    {
        const bool grouped = mOutlinerGroupRepeats;
        if (grouped)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_Header));
        if (ImGui::SmallButton(grouped ? "Grouped" : "Hierarchy"))
            mOutlinerGroupRepeats = !grouped;
        if (grouped)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                grouped
                    ? "Grouped: repeats collapse onto one row.\nClick for the "
                      "document's own hierarchy -- one row per entity."
                    : "Hierarchy: one row per entity, children under their "
                      "parent.\nClick to collapse repeats again.");
        }
    }
    ui::sameLineIfItFits(ui::buttonWidth("Expand all"));
    if (ImGui::SmallButton("Expand all"))
        mOutlinerOpenRequest = 1;
    ui::sameLineIfItFits(ui::buttonWidth("Collapse all"));
    if (ImGui::SmallButton("Collapse all"))
        mOutlinerOpenRequest = 0;

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
                   "isolate the selection -- hide everything else",
                   canIsolate) &&
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
    if (!mState.selection.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),
                           "| %zu selected", mState.selection.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("clear")) {
            mState.selection.clear();
            mSelectionAnchor.clear();
        }
    }

    ImGui::Separator();
    if (ImGui::BeginChild("##entities")) {
        // The rows themselves live in OutlinerPanel.cpp, so a headless ImGui
        // test can click them; this only says what a click should do.
        OutlinerActions actions;
        actions.isSelected = [this](const AuthorId& id) {
            return mState.isSelected(id);
        };
        actions.selectGroup = [this](const OutlinerGroup& group,
                                     SelectMode mode) {
            selectGroup(group, mode);
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
        actions.isolate = [this](const AuthorId& id) { enterIsolation(id); };
        actions.contextMenu = [this] { drawSelectionContextMenu(); };
        actions.reparent = [this](const AuthorId& child,
                                  const AuthorId& parent) {
            reparentSelection(child, parent);
        };
        actions.isHidden = [this](const AuthorId& id) {
            return mState.isHidden(id);
        };
        actions.setHidden = [this](const AuthorId& id, bool on) {
            // Descendants follow: hiding a chandelier and leaving its candles
            // floating is not hiding it.
            std::vector<AuthorId> affected{id};
            const std::vector<AuthorId> descendants =
                mState.document.descendantsOf(id);
            affected.insert(affected.end(), descendants.begin(),
                            descendants.end());
            for (const AuthorId& entry : affected)
                mState.setHidden(entry, on);
            if (on)
                mState.selection.erase(
                    std::remove_if(
                        mState.selection.begin(), mState.selection.end(),
                        [&affected](const AuthorId& selected) {
                            return std::find(affected.begin(), affected.end(),
                                             selected) != affected.end();
                        }),
                    mState.selection.end());
            if (on)
                mSelectionAnchor.clear();
            mPreview->invalidate();
        };
        actions.isLocked = [this](const AuthorId& id) {
            return mState.isLocked(id);
        };
        actions.setLocked = [this](const AuthorId& id, bool on) {
            std::vector<AuthorId> affected{id};
            const std::vector<AuthorId> descendants =
                mState.document.descendantsOf(id);
            affected.insert(affected.end(), descendants.begin(),
                            descendants.end());
            for (const AuthorId& entry : affected)
                mState.setLocked(entry, on);
            if (on)
                mState.selection.erase(
                    std::remove_if(
                        mState.selection.begin(), mState.selection.end(),
                        [&affected](const AuthorId& selected) {
                            return std::find(affected.begin(), affected.end(),
                                             selected) != affected.end();
                        }),
                    mState.selection.end());
            if (on)
                mSelectionAnchor.clear();
        };
        actions.forceOpen = mOutlinerOpenRequest;
        // Reveal whatever the world selected while the panel was not looking.
        actions.reveal = mOutlinerReveal;
        drawOutlinerRows(tree, !mOutlinerOptions.filter.empty(), actions,
                         mOutlinerRows);
        mOutlinerOpenRequest = -1;
        mOutlinerReveal.clear();
    }
    ImGui::EndChild();
    ImGui::End();
}

// --- layers -----------------------------------------------------------------
//
// Gregory §15.4.1.5. Two kinds of state meet in this panel and the split is the
// whole design: a layer's identity and who is in it are document edits and go
// through the command stack, while its visibility, lock and solo are this
// author's session and are never written to the file two people share.

void EditorApp::drawLayers()
{
    focusPanelIfRequested("layers");
    if (!ImGui::Begin(workspace_window::kLayers, nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }

    if (mLayerRowsRevision != mState.document.revision) {
        mLayerRows = layers::stats(mState.document);
        mLayerRowsRevision = mState.document.revision;
    }

    LayerActions actions;
    actions.selectionCount = mState.selection.size();
    actions.isHidden = [this](const std::string& id) {
        return layers::isHidden(mState.layerSession, id);
    };
    actions.setHidden = [this](const std::string& id, bool on) {
        layers::setHidden(mState.layerSession, id, on);
    };
    actions.isLocked = [this](const std::string& id) {
        return layers::isLocked(mState.layerSession, id);
    };
    actions.setLocked = [this](const std::string& id, bool on) {
        layers::setLocked(mState.layerSession, id, on);
    };
    actions.isSolo = [this](const std::string& id) {
        return mState.layerSession.solo == id && mState.layerSession.soloing();
    };
    actions.toggleSolo = [this](const std::string& id) {
        layers::toggleSolo(mState.layerSession, id);
    };
    actions.isActive = [this](const std::string& id) {
        return mState.layerSession.active == id;
    };
    actions.setActive = [this](const std::string& id) {
        mState.layerSession.active = id;
    };
    actions.selectMembers = [this](const std::string& id) {
        selectLayerMembers(id);
    };
    actions.assignSelection = [this](const std::string& id) {
        assignSelectionToLayer(id);
    };
    actions.rename = [this](const std::string& id, const std::string& name) {
        renameLayer(id, name);
    };
    actions.recolour = [this](const std::string& id, const glm::vec3& colour) {
        recolourLayer(id, colour);
    };
    actions.removeLayer = [this](const std::string& id) { removeLayer(id); };
    actions.addLayer = [this]() { addLayer(); };
    actions.exportLayer = [this](const std::string& id) { exportLayer(id); };
    actions.importLayer = [this](const std::string& id) {
        importIntoLayer(id);
    };

    drawLayerRows(mLayerRows, actions);
    ImGui::End();
}

// A layer's list is document data, so adding one is an undoable command like
// any other edit. The id is allocated deterministically for the same reason
// entity ids are: two authors adding a layer in parallel must not be guaranteed
// a merge conflict.
void EditorApp::addLayer()
{
    const std::string id = mState.document.allocateLayerId("layer");
    game::content::Layer layer;
    layer.id = id;
    layer.name = id;
    Command command;
    command.label = "Add layer";
    command.apply = [layer](Doc& document) {
        document.layers.push_back(layer);
        document.touch();
    };
    command.revert = [id](Doc& document) {
        for (std::size_t i = 0; i < document.layers.size(); ++i) {
            if (document.layers[i].id != id)
                continue;
            document.layers.erase(document.layers.begin() +
                                  std::ptrdiff_t(i));
            break;
        }
        document.touch();
    };
    runCommand(std::move(command));
    mState.layerSession.active = id;
    mStatus = "added layer '" + id + "'";
}

// Deleting a layer never deletes its entities: a layer is an organisation of a
// level, and losing the organisation must not lose the level. Its members fall
// back to the default layer, and undo puts both the declaration and the
// membership back.
void EditorApp::removeLayer(const std::string& layerId)
{
    if (layerId.empty())
        return; // the default layer is implicit and cannot be removed
    const game::content::Layer* found = mState.document.findLayer(layerId);
    if (!found)
        return;

    const game::content::Layer layer = *found;
    std::size_t index = 0;
    for (std::size_t i = 0; i < mState.document.layers.size(); ++i)
        if (mState.document.layers[i].id == layerId)
            index = i;
    const std::vector<AuthorId> members =
        layers::membersOf(mState.document, layerId);

    Command command;
    command.label = "Delete layer";
    command.apply = [layerId, members](Doc& document) {
        for (std::size_t i = 0; i < document.layers.size(); ++i) {
            if (document.layers[i].id != layerId)
                continue;
            document.layers.erase(document.layers.begin() + std::ptrdiff_t(i));
            break;
        }
        for (const AuthorId& id : members)
            if (Entity* entity = document.find(id))
                entity->layer.clear();
        document.touch();
    };
    command.revert = [layer, index, members](Doc& document) {
        const std::size_t at = std::min(index, document.layers.size());
        document.layers.insert(document.layers.begin() + std::ptrdiff_t(at),
                               layer);
        for (const AuthorId& id : members)
            if (Entity* entity = document.find(id))
                entity->layer = layer.id;
        document.touch();
    };
    runCommand(std::move(command));
    if (mState.layerSession.active == layerId)
        mState.layerSession.active.clear();
    if (mState.layerSession.solo == layerId)
        mState.layerSession.solo.clear();
    mStatus = "deleted layer '" + layerId + "'; its entities kept";
}

void EditorApp::renameLayer(const std::string& layerId, const std::string& name)
{
    const game::content::Layer* found = mState.document.findLayer(layerId);
    if (!found || name.empty() || found->name == name)
        return;
    const std::string before = found->name;
    Command command;
    command.label = "Rename layer";
    command.apply = [layerId, name](Doc& document) {
        if (game::content::Layer* layer = document.findLayer(layerId))
            layer->name = name;
        document.touch();
    };
    command.revert = [layerId, before](Doc& document) {
        if (game::content::Layer* layer = document.findLayer(layerId))
            layer->name = before;
        document.touch();
    };
    runCommand(std::move(command));
}

void EditorApp::recolourLayer(const std::string& layerId,
                              const glm::vec3& colour)
{
    const game::content::Layer* found = mState.document.findLayer(layerId);
    if (!found)
        return;
    const glm::vec3 before = found->colour;
    Command command;
    command.label = "Recolour layer";
    command.apply = [layerId, colour](Doc& document) {
        if (game::content::Layer* layer = document.findLayer(layerId))
            layer->colour = colour;
        document.touch();
    };
    command.revert = [layerId, before](Doc& document) {
        if (game::content::Layer* layer = document.findLayer(layerId))
            layer->colour = before;
        document.touch();
    };
    runCommand(std::move(command));
}

// "Move these forty pillars into the background layer" is the gesture layers
// exist for, so it is one undo entry rather than forty.
void EditorApp::assignSelectionToLayer(const std::string& layerId)
{
    if (mState.selection.empty())
        return;
    std::vector<std::pair<AuthorId, std::string>> before;
    before.reserve(mState.selection.size());
    for (const AuthorId& id : mState.selection)
        if (const Entity* entity = mState.document.find(id))
            before.emplace_back(id, entity->layer);
    if (before.empty())
        return;

    Command command;
    command.label = "Move to layer";
    command.apply = [before, layerId](Doc& document) {
        for (const auto& entry : before)
            if (Entity* entity = document.find(entry.first))
                entity->layer = layerId;
        document.touch();
    };
    command.revert = [before](Doc& document) {
        for (const auto& entry : before)
            if (Entity* entity = document.find(entry.first))
                entity->layer = entry.second;
        document.touch();
    };
    runCommand(std::move(command));
    mStatus = std::to_string(before.size()) + " moved to layer '" +
              (layerId.empty() ? std::string(layers::kDefaultLayerName)
                               : layerId) +
              "'";
}

void EditorApp::selectLayerMembers(const std::string& layerId)
{
    mState.selection = layers::membersOf(mState.document, layerId);
    mSelectionAnchor.clear();
    if (!mState.selection.empty())
        mOutlinerReveal = mState.selection.front();
    mStatus = std::to_string(mState.selection.size()) + " selected in layer '" +
              (layerId.empty() ? std::string(layers::kDefaultLayerName)
                               : layerId) +
              "'";
}

void EditorApp::exportLayer(const std::string& layerId)
{
    mLayerIoOpen = true;
    mLayerIoExport = true;
    mLayerIoLayer = layerId;
    mLayerIoError.clear();
    // Defaulted beside the open scene, named for the layer: the whole point is
    // handing this file to somebody, and a path they have to invent is a path
    // that lands somewhere nobody looks.
    const std::string stem =
        layerId.empty() ? std::string("default") : layerId;
    std::filesystem::path base =
        mState.scenePath.empty()
            ? std::filesystem::path(mState.assetRoot) / "scenes"
            : std::filesystem::path(mState.scenePath).parent_path();
    const std::string suggested = (base / (stem + ".layer.scn")).string();
    std::snprintf(mLayerIoPath, sizeof(mLayerIoPath), "%s", suggested.c_str());
}

void EditorApp::importIntoLayer(const std::string& layerId)
{
    mLayerIoOpen = true;
    mLayerIoExport = false;
    mLayerIoLayer = layerId;
    mLayerIoError.clear();
    mLayerIoPath[0] = '\0';
}

void EditorApp::drawLayerIoPopup()
{
    static constexpr const char* kTitle = "Layer file###LayerIo";
    if (mLayerIoOpen && !ImGui::IsPopupOpen(kTitle))
        ImGui::OpenPopup(kTitle);
    if (!ImGui::BeginPopupModal(kTitle, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const std::string name = layers::displayName(mState.document, mLayerIoLayer);
    if (mLayerIoExport)
        ImGui::Text("Write layer '%s' to:", name.c_str());
    else
        ImGui::Text("Merge a layer file into '%s':", name.c_str());
    ImGui::SetNextItemWidth(520.0f);
    ImGui::InputText("##path", mLayerIoPath, sizeof(mLayerIoPath));
    if (mLayerIoExport)
        ImGui::TextDisabled("Only this layer's entities, plus the ancestors "
                            "they hang from as bare transforms.");
    else
        ImGui::TextDisabled("Colliding ids are renamed, never overwritten.");
    if (!mLayerIoError.empty())
        ImGui::TextColored(ImVec4(0.89f, 0.42f, 0.33f, 1.0f), "%s",
                           mLayerIoError.c_str());

    if (ImGui::Button(mLayerIoExport ? "Export" : "Import")) {
        std::string error;
        if (mLayerIoExport) {
            const Doc slice =
                layers::extractLayer(mState.document, mLayerIoLayer);
            if (game::content::writeSceneSource(mLayerIoPath, slice, error)) {
                mStatus = "exported " + std::to_string(slice.entities.size()) +
                          " entities to " + mLayerIoPath;
                mLayerIoOpen = false;
                ImGui::CloseCurrentPopup();
            } else {
                mLayerIoError = error;
            }
        } else {
            Doc incoming;
            if (!game::content::loadSceneSource(mLayerIoPath, incoming, error)) {
                mLayerIoError = error;
            } else {
                // Merged once, here, so the report can be shown; the command
                // then swaps whole documents. Both halves are captured by
                // value because a merge touches ids, parents and the layer
                // list at once, and reversing that edit-by-edit is where the
                // bugs would be. A document is a vector of PODs -- two copies
                // of a level cost less than the bookkeeping would.
                const Doc before = mState.document;
                Doc after = before;
                const layers::MergeReport report =
                    layers::mergeLayer(after, incoming, mLayerIoLayer);

                const auto swap = [](Doc& document, const Doc& to) {
                    const uint64_t revision = document.revision;
                    document = to;
                    document.revision = revision;
                    document.touch();
                };
                Command command;
                command.label = "Import layer";
                command.apply = [after, swap](Doc& document) {
                    swap(document, after);
                };
                command.revert = [before, swap](Doc& document) {
                    swap(document, before);
                };
                runCommand(std::move(command));
                mStatus = "merged " + std::to_string(report.added) +
                          " entities" +
                          (report.renamed.empty()
                               ? std::string()
                               : ", " + std::to_string(report.renamed.size()) +
                                     " renamed");
                mLayerIoOpen = false;
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        mLayerIoOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Shared by both outliner rows: whatever is selected, act on all of it. Adding
// a component here is the same call the inspector makes, so there is one path
// from "author wants a collider" to the document.
void EditorApp::drawSelectionContextMenu()
{
    if (ImGui::MenuItem("Focus", "F"))
        frameSelectionOrAll();
    if (ImGui::MenuItem("Isolate", "double-click", false,
                        mState.primary() != nullptr))
        enterIsolation(*mState.primary());
    if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        duplicateSelection();

    // Only offered on a compound piece that still has its parts baked: on
    // anything else the row would be a dead item explaining nothing.
    if (const AuthorId* primary = mState.primary();
        primary && canUnpackAttachments(*primary)) {
        if (ImGui::MenuItem("Unpack attachments"))
            unpackAttachments(*primary);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("write this piece's attached parts out as child "
                              "entities, so they can be selected and moved");
    }

    // Parenting from the menu as well as by dragging: a drag is the fast path
    // once you know it exists, and it is not a gesture anyone discovers.
    const bool canGroup = mState.selection.size() > 1;
    const std::string parentLabel =
        mState.primary() ? "Parent selection to '" + *mState.primary() + "'"
                         : std::string("Parent selection");
    if (ImGui::MenuItem(parentLabel.c_str(), nullptr, false, canGroup))
        parentSelectionToPrimary();
    bool anyParented = false;
    for (const AuthorId& id : mState.selection)
        if (const Entity* entity = mState.document.find(id))
            anyParented = anyParented || !entity->parent.empty();
    if (ImGui::MenuItem("Detach from parent", nullptr, false, anyParented))
        detachSelection();
    if (ImGui::BeginMenu("Add Component")) {
        const ComponentDefaults defaults = componentDefaults();
        std::vector<const Entity*> selected;
        selected.reserve(mState.selection.size());
        for (const AuthorId& id : mState.selection)
            if (const Entity* entity = mState.document.find(id))
                selected.push_back(entity);
        for (const ComponentType& type : componentTypes()) {
            if (!type.add)
                continue;
            // A component that means nothing on any of these entities is not
            // offered at all -- the sound table on a wall being the case this
            // exists for.
            if (type.applies) {
                bool any = false;
                for (const Entity* entity : selected)
                    any = any || type.applies(*entity);
                if (!any)
                    continue;
            }
            const bool ready = !type.addable || type.addable(defaults);
            if (ImGui::MenuItem(type.label, nullptr, false, ready))
                addComponentToSelection(type);
            if (!ready && ImGui::IsItemHovered())
                ImGui::SetTooltip("pick a piece in Asset Browser first");
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
    defaults.prefab = mState.brush.kind == Brush::Kind::Piece
                          ? mState.brush.prefab
                          : std::string();
    // The brush, which is what a click in Placeables now sets: picking a mesh
    // row there and then adding a Mesh component in the inspector is the same
    // intent as painting one.
    defaults.meshPath = mState.brush.kind == Brush::Kind::Mesh
                            ? mState.brush.meshPath
                            : std::string();
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
        // Mixed selections are normal (drag a box over a room and you get the
        // enemies and the walls it stands on). The component lands only on the
        // entities it means something for.
        if (type.applies && !type.applies(*entity))
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
    if (std::string_view(type.id) == "audio" &&
        std::find(mState.selection.begin(), mState.selection.end(),
                  mAudioPreviewEntity) != mState.selection.end())
        stopAudioPreview();
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

Entity EditorApp::makeGameplayEntity(Gameplay kind,
                                     const XformAuthor& transform) const
{
    Entity entity;
    entity.transform = transform;

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
    case Gameplay::Portal:
        stem = "portal";
        // Left unset when the kit has no membrane, which the validator then
        // reports as a missing prefab. Refusing to build the entity at all was
        // the old behaviour and it lost the author's click silently.
        if (mState.catalog.find("kit.portal_membrane"))
            entity.prefab = "kit.portal_membrane";
        entity.portal = PortalAuthor{};
        entity.exitYawDegrees = 0.0f;
        entity.castShadows = false;
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
    case Gameplay::Npc:
        stem = "npc";
        component = "npc";
        break;
    case Gameplay::Trigger:
        stem = "trigger";
        component = "trigger";
        break;
    case Gameplay::AudioEmitter:
        stem = "audio";
        component = "audio";
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

    // A light placed on the floor lights the floor. Lifting it is what makes a
    // freshly dropped one do anything visible -- and now that placement follows
    // the cursor onto geometry, the lift is measured from whatever it landed
    // on rather than from the work plane.
    if (kind == Gameplay::PointLight)
        entity.transform.position.y += 3.0f;
    if (kind == Gameplay::DirectionalLight) {
        // A key light is aimed, not placed: it is the rotation that matters,
        // and the height only keeps the gizmo out of the floor.
        entity.transform.position.y += 8.0f;
        entity.transform.rotationDegrees = {-55.0f, 30.0f, 0.0f};
        entity.light = LightAuthor{LightAuthor::Type::Directional,
                                   {0.95f, 0.93f, 0.88f},
                                   0.0f,
                                   true,
                                   std::nullopt};
    }
    entity.id = mState.document.allocateId(stem);
    entity.name = entity.id;
    return entity;
}

// The one-shot path, still used by the directional light and the command
// palette: build it in front of the camera and select it.
void EditorApp::addGameplayEntity(Gameplay kind)
{
    XformAuthor transform;
    transform.position = viewFocusPoint();
    if (mState.gridState.snap) {
        const float step = mState.gridState.step();
        transform.position.x = std::round(transform.position.x / step) * step;
        transform.position.z = std::round(transform.position.z / step) * step;
    }

    Entity entity = makeGameplayEntity(kind, transform);
    adoptIntoIsolation(entity);
    stampLayer(entity);
    runCommand(makeCreateEntity(entity));
    selectAndReveal(entity.id, false);
    mPreview->invalidate();
}

void EditorApp::drawAssetBrowser()
{
    const bool requested =
        mFocusPanelFrames > 0 &&
        (mFocusPanel == "catalog" || mFocusPanel == "meshes" ||
         mFocusPanel == "material" || mFocusPanel == "particles" ||
         mFocusPanel == "resourcedb");
    if (requested) {
        ImGui::SetNextWindowFocus();
        // "meshes" still resolves, to Placeables: the meshes are in it now, and
        // a verification hook that started failing because a tab was merged
        // would be reporting on the hook rather than on the editor.
        mAssetBrowserModeRequest = mFocusPanel == "material"     ? 1
                                   : mFocusPanel == "particles"  ? 2
                                   : mFocusPanel == "resourcedb" ? 3
                                                                 : 0;
    }
    if (!ImGui::Begin(workspace_window::kFileSystem, nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }

    if (mMaterialNames.empty() && mEngine) {
        mMaterialNames = mEngine->renderer().materialNames();
        std::sort(mMaterialNames.begin(), mMaterialNames.end());
    }
    if (ImGui::BeginTabBar("##asset_modes",
                           ImGuiTabBarFlags_FittingPolicyScroll)) {
        const auto flagsFor = [this](int mode) -> ImGuiTabItemFlags {
            return mAssetBrowserModeRequest == mode
                       ? ImGuiTabItemFlags_SetSelected
                       : 0;
        };
        // Placeables is everything that can go in a level -- kit pieces,
        // gameplay entities, generated primitives and every mesh file in the
        // project. There is no second geometry tab: a Meshes tab beside this
        // one split the same question ("what can I put here") by where the
        // asset came from, which is the one thing an author placing it does not
        // care about.
        if (ImGui::BeginTabItem("Placeables", nullptr, flagsFor(0))) {
            drawCatalog();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Materials", nullptr, flagsFor(1))) {
            drawMaterialPanel();
            ImGui::EndTabItem();
        }
        const std::string effectsLabel =
            mParticlesDirty ? "Effects *" : "Effects";
        if (ImGui::BeginTabItem(effectsLabel.c_str(), nullptr, flagsFor(2))) {
            drawParticlePanel();
            ImGui::EndTabItem();
        }
        // The Resource Database Management Tool. Last, because it answers a
        // different question from the other three: not "what can I place" but
        // "what does the engine know about, and is it built".
        if (ImGui::BeginTabItem("Resource DB", nullptr, flagsFor(3))) {
            mResourceDb.draw();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    mAssetBrowserModeRequest = -1;
    ImGui::End();
}

// Placeables: everything that can be put in a level, in one list.
//
// This is the only route into a scene. It used to be two -- kit pieces and
// gameplay entities here, mesh files and generated primitives in a Meshes tab
// beside it -- and the split was about where an asset came from, which is the
// one thing an author placing it does not care about. Worse, the two tabs
// behaved differently: a click here armed the brush, a click there only
// selected a row and left you hunting for "Use as Brush".
//
// Now: gameplay entities, kit pieces, generated primitives, and every mesh file
// in the project, all in one filtered list, and a click on any row arms the
// Place tool with it. Adding geometry to a level is one gesture with one
// vocabulary.
//
// Layout is the shared asset-panel shape (preview, metadata, actions, toggles,
// list); the subject of all of it is the BRUSH, because the brush is now what a
// selection in this panel means.
void EditorApp::drawCatalog()
{
    eng::Renderer& renderer = mEngine->renderer();
    if (!mStage.thumbnailBuilt())
        mStage.buildThumbnail(renderer, 256);
    if (mParticleThumbnailNode.valid())
        renderer.setNodeVisible(mParticleThumbnailNode, false);
    mStage.setThumbnailVisible(renderer, true);

    const MeshCatalog& meshes = meshCatalog();
    const KitPiece* piece = mState.brush.kind == Brush::Kind::Piece
                                ? mState.catalog.find(mState.brush.prefab)
                                : nullptr;
    const MeshAsset* meshAsset = mState.brush.kind == Brush::Kind::Mesh
                                     ? meshes.find(mState.brush.meshPath)
                                     : nullptr;
    // The swatch follows the brush unless a row is being hovered, which the
    // list below re-requests every frame while the cursor is over it.
    if (piece && !piece->isGroup())
        requestMeshPreview(piece->meshPath, piece->material);
    else if (mState.brush.kind == Brush::Kind::Mesh)
        requestMeshPreview(mState.brush.meshPath,
                           meshAsset ? meshAsset->material : std::string());
    else if (mState.brush.kind == Brush::Kind::Primitive)
        requestPrimitivePreview(mState.brush.primitive);

    ed::ui::AssetPanelView view;
    // A gameplay brush has no mesh, so it has no swatch either. The space goes
    // to the metadata rather than showing the last piece hovered, which would
    // claim a marker looks like a barrel.
    const bool hasSwatch = mState.brush.kind != Brush::Kind::Gameplay &&
                           mState.brush.kind != Brush::Kind::Particles &&
                           !(piece && piece->isGroup());
    view.previewTexture = hasSwatch ? renderer.materialThumbnailTextureId() : 0;
    view.previewTooltip =
        "The brush, in its own material. Drag to turn; hovering a row previews "
        "it, clicking arms it.";
    view.onPreviewDrag = [&](float dx) {
        mThumbAutoSpin = false;
        mStage.spinThumbnail(renderer, mStage.thumbnailSpin() + dx * 0.01f);
    };

    view.metadata = [&] {
        switch (mState.brush.kind) {
        case Brush::Kind::Gameplay:
            ImGui::TextUnformatted(gameplayName(mState.brush.gameplay));
            ImGui::TextDisabled("gameplay entity | no mesh");
            break;
        case Brush::Kind::Particles:
            ImGui::TextUnformatted(mState.brush.effect.empty()
                                       ? "(no particle effect)"
                                       : mState.brush.effect.c_str());
            ImGui::TextDisabled("particle effect");
            break;
        case Brush::Kind::Mesh: {
            ImGui::TextUnformatted(
                meshAsset ? meshAsset->name.c_str()
                          : mState.brush.meshPath.c_str());
            ImGui::TextDisabled("%s", mState.brush.meshPath.c_str());
            if (meshAsset)
                ImGui::TextDisabled("%s | %s", meshAsset->extension.c_str(),
                                    ed::ui::humanBytes(meshAsset->sizeBytes)
                                        .c_str());
            drawMeshGeometryInfo(mState.brush.meshPath);
            if (meshAsset && !meshAsset->kitPrefab.empty()) {
                ImGui::TextColored(kUiWarning, "in the kit as %s",
                                   meshAsset->kitPrefab.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Placing the kit piece instead brings its material, "
                        "socket and grid snapping with it. Place the raw mesh "
                        "only when you want none of that.");
            }
            break;
        }
        case Brush::Kind::Primitive:
            ImGui::TextUnformatted(
                eng::ecs::primitiveKindName(mState.brush.primitive.kind));
            ImGui::TextDisabled("generated | no file -- built by the engine at "
                                "load");
            break;
        case Brush::Kind::Piece:
            if (!piece) {
                ImGui::TextUnformatted("(nothing selected)");
                ImGui::TextDisabled("pick a row below to arm the Place tool");
                break;
            }
            ImGui::TextUnformatted(piece->id.c_str());
            if (piece->isGroup()) {
                // A model, not a mesh: what it is made of is the useful fact.
                ImGui::TextDisabled("%s | model of %zu part(s)",
                                    piece->role.c_str(),
                                    piece->attachments.size());
            }
            else {
                ImGui::TextDisabled("%s | socket %s | span %d",
                                    piece->role.c_str(),
                                    socketName(piece->socket), piece->span);
                ImGui::TextDisabled("%s", piece->material.c_str());
                if (!piece->attachments.empty())
                    ImGui::TextDisabled("+ %zu attached part(s)",
                                        piece->attachments.size());
            }
            break;
        }
        // Rotation and size apply to every brush kind, so they are stated once
        // here rather than per section.
        ImGui::TextDisabled("rot %d deg (wheel)  |  size %.2fx (ctrl+wheel)",
                            mState.brush.yawQuarters * 90,
                            double(mState.brush.scale));
    };

    view.actions = [&] {
        const bool haveSelection = !mState.selection.empty();
        // Applying to the selection is the other thing a browser row is for:
        // "make these forty pillars that mesh instead". It is an edit, not a
        // brush, which is why it stays a button rather than a click.
        if (mState.brush.kind == Brush::Kind::Mesh) {
            ImGui::BeginDisabled(!haveSelection);
            if (ImGui::Button("Apply to Selection"))
                applyMeshToSelection(mState.brush.meshPath);
            ImGui::EndDisabled();
            // The route back to the kit, for the common case where the mesh IS
            // a kit piece and the piece is what the author actually wants.
            if (meshAsset && !meshAsset->kitPrefab.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Place as Kit Piece")) {
                    leaveMaterialStage();
                    mState.brush.kind = Brush::Kind::Piece;
                    mState.brush.prefab = meshAsset->kitPrefab;
                    mState.tool = Tool::Place;
                    mStatus = "kit brush: " + meshAsset->kitPrefab;
                }
            }
        }
        else if (mState.brush.kind == Brush::Kind::Primitive) {
            ImGui::BeginDisabled(!haveSelection);
            if (ImGui::Button("Apply to Selection"))
                applyPrimitiveToSelection(mState.brush.primitive);
            ImGui::EndDisabled();
        }
        else if (piece) {
            ImGui::TextDisabled("click to paint  |  wheel turns, ctrl+wheel "
                                "resizes");
        }
    };

    view.toggles = [&] {
        ImGui::Checkbox("spin", &mThumbAutoSpin);
        ImGui::SameLine();
        ImGui::Checkbox("hide kit meshes", &mHideKitMeshes);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Most of assets/meshes is the kit. Placing those as raw "
                "meshes loses their material and their grid snapping.");
        ImGui::Checkbox("place parts as children", &mPlaceUnpacksParts);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "A model made of parts arrives as an object you can open: the "
                "root and every part are real entities in the hierarchy, each "
                "selectable and movable. Off, the parts are generated at cook "
                "time and cannot be edited.");

        // The brush's own size, beside its own preview, because it is the
        // number the ghost is drawn at.
        ed::ui::PropertyGrid grid("##brush_size", 0.34f);
        grid.row("brush size", "x");
        ImGui::DragFloat("##brush_scale", &mState.brush.scale, 0.01f,
                         Brush::kMinScale, Brush::kMaxScale, "%.2f");
        mState.brush.scale =
            std::clamp(mState.brush.scale, Brush::kMinScale, Brush::kMaxScale);
        // The primitive's own parameters, edited here so a size is authored
        // once and painted, rather than painted and then corrected per entity.
        if (mState.brush.kind == Brush::Kind::Primitive)
            drawPrimitiveFields(mState.brush.primitive, grid);
    };

    view.filter = mCatalogFilter;
    view.filterCapacity = sizeof(mCatalogFilter);
    view.filterHint = "Search placeables...";

    std::size_t pieceCount = 0;
    for (const std::string& role : mState.catalog.roles())
        pieceCount += mState.catalog.byRole(role).size();
    std::size_t hiddenMeshes = 0;
    for (const MeshAsset& asset : meshes.all())
        if (mHideKitMeshes && !asset.kitPrefab.empty())
            ++hiddenMeshes;
    view.footer = std::to_string(pieceCount) + " pieces  |  " +
                  std::to_string(paintableGameplay().size()) + " gameplay  |  " +
                  std::to_string(meshes.all().size() - hiddenMeshes) + " of " +
                  std::to_string(meshes.all().size()) + " meshes";

    view.list = [&] {
        const std::string filter = mCatalogFilter;

        // Gameplay entities are authored here too: they are part of the level's
        // vocabulary even though they have no mesh, and selecting one arms the
        // Place tool exactly as selecting a wall does.
        ImGui::SeparatorText("gameplay");
        for (const Gameplay kind : paintableGameplay()) {
            if (!ed::ui::filterMatches(gameplayName(kind), filter))
                continue;
            const bool active = mState.brush.kind == Brush::Kind::Gameplay &&
                                mState.brush.gameplay == kind;
            if (ImGui::Selectable(gameplayName(kind), active)) {
                mState.brush.kind = Brush::Kind::Gameplay;
                mState.brush.gameplay = kind;
                mState.tool = Tool::Place;
            }
        }
        // The one exception: a key light is aimed rather than positioned, so
        // painting a row of them down a corridor is not a thing anyone means to
        // do. It stays a button.
        if (ed::ui::filterMatches("directional light", filter)) {
            if (ImGui::Button("directional light (one-shot)",
                              ImVec2(-1.0f, 0.0f)))
                addGameplayEntity(Gameplay::DirectionalLight);
        }

        // Grouped by role, which is how kit.toml is authored and how an author
        // thinks: "I need a wall", not "I need piece 17".
        for (const std::string& role : mState.catalog.roles()) {
            std::vector<const KitPiece*> shown;
            for (const KitPiece* candidate : mState.catalog.byRole(role)) {
                if (ed::ui::filterMatches(candidate->id, filter) ||
                    ed::ui::filterMatches(role, filter))
                    shown.push_back(candidate);
            }
            if (shown.empty())
                continue;
            ImGui::SeparatorText(
                eng::assets::friendlyAssetLabel(role).c_str());
            for (const KitPiece* candidate : shown) {
                const bool active = mState.brush.kind == Brush::Kind::Piece &&
                                    mState.brush.prefab == candidate->id;
                const std::string label =
                    eng::assets::friendlyAssetLabel(candidate->id) + "###" +
                    candidate->id;
                if (ImGui::Selectable(label.c_str(), active)) {
                    leaveMaterialStage();
                    mState.brush.kind = Brush::Kind::Piece;
                    mState.brush.prefab = candidate->id;
                    mState.tool = Tool::Place;
                }
                // Hovering previews, selecting commits -- the same split every
                // list in this browser uses.
                if (ImGui::IsItemHovered() && !candidate->isGroup())
                    requestMeshPreview(candidate->meshPath,
                                       candidate->material);
                char detail[512];
                if (candidate->isGroup())
                    std::snprintf(detail, sizeof(detail),
                                  "model of %zu part(s)\nplaces as one object",
                                  candidate->attachments.size());
                else
                    std::snprintf(detail, sizeof(detail),
                                  "socket %s  span %d\n%s",
                                  socketName(candidate->socket),
                                  candidate->span,
                                  candidate->meshPath.c_str());
                eng::imguihint::showText(candidate->id.c_str(), detail);
            }
        }

        // Generated: they need no file and they are the fastest way to block
        // out a room.
        ImGui::SeparatorText("generated");
        for (const PrimitivePreset& preset : primitivePresets()) {
            if (!ed::ui::filterMatches(preset.label, filter) &&
                !ed::ui::filterMatches(preset.id, filter))
                continue;
            const bool active =
                mState.brush.kind == Brush::Kind::Primitive &&
                mState.brush.primitive.kind == preset.mesh.kind;
            if (ImGui::Selectable(preset.label, active)) {
                // The preset's parameters become the brush, so choosing
                // "Capsule" gives a person-shaped one rather than whatever the
                // last kind left in the fields.
                usePrimitiveBrush(preset.mesh);
            }
            if (ImGui::IsItemHovered()) {
                requestPrimitivePreview(preset.mesh);
                ImGui::SetTooltip("%s\n%s", preset.label, preset.hint);
            }
        }

        // Every mesh file in the project. Last, because it is the longest list
        // and the least specific: a kit piece brings a material and a socket, a
        // raw mesh brings neither, so the kit is the better answer whenever it
        // has one.
        std::string group = "\x01"; // no group can equal this
        for (const MeshAsset& asset : meshes.all()) {
            if (mHideKitMeshes && !asset.kitPrefab.empty())
                continue;
            if (!ed::ui::filterMatches(asset.name, filter) &&
                !ed::ui::filterMatches(asset.path, filter))
                continue;
            if (asset.group != group) {
                group = asset.group;
                ImGui::SeparatorText(group.empty() ? "meshes"
                                                   : ("mesh: " + group).c_str());
            }
            const std::string label = asset.name + "###" + asset.path;
            const bool active = mState.brush.kind == Brush::Kind::Mesh &&
                                mState.brush.meshPath == asset.path;
            if (ImGui::Selectable(label.c_str(), active))
                useMeshBrush(asset.path);
            if (ImGui::IsItemHovered()) {
                requestMeshPreview(asset.path, asset.material);
                ImGui::SetTooltip("%s\n%s%s", asset.path.c_str(),
                                  asset.material.empty()
                                      ? "no authored material"
                                      : asset.material.c_str(),
                                  asset.kitPrefab.empty()
                                      ? ""
                                      : "\nalso a kit piece");
            }
        }
    };

    ed::ui::drawAssetPanel(view);
}


// The level's own properties: what it is called, what it costs, and the look it
// is lit and graded with.
//
// Shown in the Inspector when nothing is selected, which is where every engine
// puts world settings and which was two lines of dead space here.
// What the Inspector shows with nothing selected.
//
// Deliberately thin, and it did not used to be: it carried the level's palette,
// which is the Environment role and belongs beside the other five in the
// Contract dock. Two panels editing one field is how they end up disagreeing
// about which of them owns it.
void EditorApp::drawSceneProperties()
{
    ImGui::TextDisabled("Nothing selected.");
    ImGui::TextWrapped(
        "Click an entity in the viewport or the Scene tree to edit it.");
    ImGui::Spacing();
    ImGui::SeparatorText("this scene");

    const ContractReport& report = contract();
    ImGui::Text("%s", sceneKindName(report.kind));
    ImGui::SameLine();
    ImGui::TextDisabled("scene, %zu entities",
                        mState.document.entities.size());
    ImGui::TextWrapped("%s", sceneKindSummary(report.kind));
    ImGui::Spacing();
    if (ImGui::Button("Open the Contract panel"))
        requestPanelFocus("contract");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("its kind, the roles it fills, what is missing, and "
                          "the palette it is graded with");
}

// Changing the palette is a document edit like any other, so it undoes.
void EditorApp::setScenePalette(const std::string& palette)
{
    if (mState.document.palette == palette)
        return;
    const std::string before = mState.document.palette;
    runCommand(Command{palette.empty() ? "clear palette" : "palette " + palette,
                       [palette](Doc& doc) { doc.palette = palette; },
                       [before](Doc& doc) { doc.palette = before; }});
    if (mEngine)
        applySceneEnvironment(mEngine->renderer());
}

void EditorApp::drawInspector()
{
    focusPanelIfRequested("inspector");
    if (!ImGui::Begin(workspace_window::kInspector, nullptr, kPanelFlags)) {
        finishInspectorEdit();
        ImGui::End();
        return;
    }
    const AuthorId* primary = mState.primary();
    Entity* entity = primary ? mState.document.find(*primary) : nullptr;
    if (!entity) {
        finishInspectorEdit();
        // With nothing selected the panel shows the level itself. It used to
        // show two lines of nothing, and the level's own properties -- the
        // palette it is lit and graded with -- had no home at all.
        drawSceneProperties();
        ImGui::End();
        return;
    }
    if (mInspectorEdit.active() && mInspectorEdit.id() != entity->id) {
        finishInspectorEdit();
        entity = mState.document.find(*primary);
        if (!entity) {
            ImGui::End();
            return;
        }
    }

    std::vector<const Entity*> selectedEntities;
    selectedEntities.reserve(mState.selection.size());
    for (const AuthorId& id : mState.selection)
        if (const Entity* selected = mState.document.find(id))
            selectedEntities.push_back(selected);
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
    context.grid = &mState.grid;
    // What the whole selection agrees about, so the transform rows can show
    // "--" where it does not (Gregory §15.4.1.6). Computed here rather than in
    // the panel because only the app knows what is selected.
    if (mState.selection.size() > 1) {
        std::vector<const Entity*> selected;
        selected.reserve(mState.selection.size());
        for (const AuthorId& id : mState.selection)
            if (const Entity* e = mState.document.find(id))
                selected.push_back(e);
        context.agreement = multiedit::agreementOf(selected);
    }
    context.materialNames = &mMaterialNames;
    context.enemyIds = &mEnemyIds;
    context.scriptPaths = &mScriptPaths;
    // Making a script, and opening one. The editor owns both because only it
    // knows where the project's scripts/ is; see ScriptWorkshop.h.
    context.createScript = [this](const std::string& entityName) -> std::string {
        const std::string logical = suggestedScriptPath(entityName);
        const std::filesystem::path file =
            std::filesystem::path(mState.assetRoot) / logical;
        std::string error;
        if (!createScript(file.string(), classNameFromPath(logical), error)) {
            mStatus = error;
            return {};
        }
        mStatus = "created " + logical;
        rescanScriptPaths();
        return logical;
    };
    context.openScript = [this](const std::string& logical) {
        const std::filesystem::path file =
            std::filesystem::path(mState.assetRoot) / logical;
        std::string error;
        if (!openInExternalEditor(file.string(), error))
            mStatus = error;
    };
    context.rescanScripts = [this] { rescanScriptPaths(); };
    // Rebuilt per frame: an entity added this frame is a legitimate target for
    // a script prop, and a cached list would not offer it.
    mSceneEntityIds.clear();
    for (const Entity& e : mState.document.entities)
        mSceneEntityIds.push_back(e.id);
    context.sceneEntityIds = &mSceneEntityIds;
    context.pickupIds = &mPickupIds;
    context.npcIds = &mNpcIds;
    context.traderIds = &mTraderIds;
    context.questIds = &mQuestIds;
    context.stationIds = &mStationIds;
    context.weaponIds = &mWeaponIds;
    context.materials = &materialCatalog();
    context.meshKind = selectionMeshKind();
    // Rebuilt each frame from the live library: the Particles panel can add and
    // rename effects, and a stale list would offer a name that no longer
    // resolves -- which plays nothing, silently.
    mParticleEffectNames.clear();
    for (const eng::ParticleEffectDesc& d : mParticles.descs())
        mParticleEffectNames.push_back(d.name);
    context.particleEffects = &mParticleEffectNames;
    context.audioAssets = &mAudioAssets;
    context.audioCues = &mAudioCues;
    context.previewTexture = mEngine->renderer().materialThumbnailTextureId();
    context.requestMaterialPreview = [this](const std::string& name) {
        requestMaterialPreview(name);
    };
    context.requestEffectPreview = [this](const std::string& name) {
        requestEffectPreview(name);
    };
    context.requestAudioPreview = [this](const AuthorId& id) {
        previewAudio(id);
    };
    context.stopAudioPreview = [this] { stopAudioPreview(); };
    context.audioPreviewing =
        mAudioPreview && mAudioPreview->isPlaying() &&
        mAudioPreviewEntity == entity->id;

    // The swatch, first thing in the panel: "what does this look like" is the
    // question an inspector is opened to answer, and the answer used to live in
    // a different dock column. Shows the entity's effective material -- its own
    // override, or the kit piece's when it has none.
    {
        std::string worn = entity->material;
        if (worn.empty())
            if (const KitPiece* piece = mState.catalog.find(entity->prefab))
                worn = piece->material;
        if (!worn.empty()) {
            requestMaterialPreview(worn);
            if (context.previewTexture != 0) {
                const float side = std::clamp(
                    ImGui::GetContentRegionAvail().x * 0.34f, 72.0f, 112.0f);
                ImGui::Image(static_cast<ImTextureID>(context.previewTexture),
                             ImVec2(side, side));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(worn.c_str());
                ImGui::TextDisabled(entity->material.empty() ? "from kit"
                                                             : "override");
                ImGui::EndGroup();
                ImGui::Separator();
            }
        }
    }

    // Entity identity scopes every widget. Switching selection while a text or
    // numeric field is active can no longer transfer ImGui's active input state
    // to a same-named field on the next entity.
    ImGui::PushID(entity->id.c_str());
    drawEntityIdentity(*entity, context);

    // One collapsing section per component the entity carries, straight off the
    // registry. The panel has no idea what a light or a trigger is: adding a
    // component type means one entry in EntityComponents.cpp and one drawer in
    // ComponentInspector.cpp, and this loop picks it up.
    // Grouped and always in the same order (ComponentGroup): appearance,
    // physical, gameplay, placement. Before, sections came out in table order,
    // so "where is the material" depended on which entity was selected -- and
    // the answer to a fixed question moving around the screen is what makes a
    // panel unscannable.
    const ComponentType* removeRequested = nullptr;
    ComponentGroup band = ComponentGroup::Placement;
    bool first = true;
    for (const ComponentType* type : componentsOf(*entity)) {
        if (first || type->group != band) {
            band = type->group;
            first = false;
            ImGui::Spacing();
            ImGui::TextDisabled("%s", componentGroupName(band));
        }
        ImGui::PushID(type->id);
        // Collapsible, and open by default: a dense entity was one long scroll
        // with no way to fold away the part being ignored, and the parts an
        // author is not editing are most of it.
        const ComponentPresence presence =
            componentPresence(*type, selectedEntities);
        const std::string header =
            presence.mixed() ? std::string(type->label) + "  (" +
                                   std::to_string(presence.present) + "/" +
                                   std::to_string(presence.total) + ")"
                             : std::string(type->label);
        const bool open = ImGui::CollapsingHeader(
            header.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        if (type->remove) {
            // Right-aligned so the sections read as a column of headers rather
            // than a column of buttons.
            ImGui::SameLine(ImGui::GetContentRegionAvail().x -
                            ImGui::GetFrameHeight());
            if (ImGui::SmallButton("x"))
                removeRequested = type;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("remove %s from the selection", type->label);
        }
        if (open) {
            ImGui::Indent(8.0f);
            drawComponentBody(*type, *entity, context);
            ImGui::Unindent(8.0f);
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    // Ctrl+Shift+A avoids stealing Ctrl+A from the Name field or exact numeric
    // entry. No panel shortcut fires while another item owns input.
    const bool addShortcut =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsAnyItemActive() &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_A);
    if (ImGui::Button("Add Component   (Ctrl+Shift+A)", ImVec2(-1.0f, 0.0f)) ||
        addShortcut)
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
        ComponentGroup menuBand = ComponentGroup::Placement;
        bool menuFirst = true;
        for (const ComponentType* type : missingComponents(selectedEntities)) {
            if (!filter.empty() &&
                std::string(type->label).find(filter) == std::string::npos &&
                std::string(type->id).find(filter) == std::string::npos)
                continue;
            // Same bands as the panel below it, so the menu is a map of where
            // the thing you are adding will appear.
            if (menuFirst || type->group != menuBand) {
                menuBand = type->group;
                menuFirst = false;
                if (offered > 0)
                    ImGui::Separator();
                ImGui::TextDisabled("%s", componentGroupName(menuBand));
            }
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

    if (context.edited) {
        if (!mInspectorEdit.active())
            mInspectorEdit.begin(before);
        mState.document.touch();
        // An active drag is already unsaved work even before the pointer is
        // released and the one history command is recorded.
        mState.dirty = true;
        mCookStatus = "stale";
    }
    if (context.closed)
        finishInspectorEdit();
    // Deferred to here: removing a component invalidates `entity` through the
    // command stack, and the section loop above is still holding it.
    if (removeRequested)
        removeComponentFromSelection(*removeRequested);
    ImGui::PopID();
    ImGui::End();
}

// A bottom-panel body rather than a window: the timeline is read against the
// viewport above it while something plays, which is exactly the shape a
// full-width region has and a dock tab does not.
void EditorApp::drawTimeline()
{
    // The panel edits the PREVIEW world's clips, not the document's.
    //
    // That is the honest state of this feature and worth saying plainly: the
    // preview is rebuilt from the document on every edit, so a clip scrubbed
    // here is a clip the author is *watching*, and retiming a key is lost on
    // the next rebuild. Authoring a clip into the .scn needs the Clip component
    // in the inspector, which is the next step and is not built. Previewing is
    // still the half that cannot be done any other way -- a timeline you cannot
    // play is not a timeline.
    mClipPanel.setSources(mPreview ? &mPreview->world() : nullptr,
                          &mapio::coreRegistry());
    ImGui::TextDisabled(
        "Previews clips in the viewport. Edits are not saved to the scene "
        "yet.");
    ImGui::Separator();
    mClipPanel.drawBody();
}

// What this scene IS.
//
// A dock beside the Inspector, which is a promotion: the same table used to be
// drawn at the top of the Problems tab, where "I opened a scene and I do not
// know what it does" is a question nobody thinks to ask. Here it is one click
// from the properties of whatever is selected, and it is where the workflow
// starts -- the kind decides which main screen opens, whether Play is allowed,
// and what the roles below are judged against.
//
// One table, shared with the validator and the cooker, so a person reading this
// and a build failing cannot disagree. See docs/scenes.md.
void EditorApp::drawContract()
{
    focusPanelIfRequested("contract");
    if (!ImGui::Begin(workspace_window::kContract, nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }

    const ContractReport& report = contract();

    // The kind, and what it means. A scene nobody can look through is the one
    // state worth shouting about, so it is the only one drawn in the danger
    // colour.
    const bool empty = report.kind == SceneKind::Empty;
    if (empty)
        ImGui::TextColored(kUiDanger, "%s", sceneKindName(report.kind));
    else
        ImGui::Text("%s", sceneKindName(report.kind));
    ImGui::SameLine();
    ImGui::TextDisabled("scene");
    ImGui::SameLine();
    if (report.playable)
        ImGui::TextDisabled("| playable");
    else
        ImGui::TextColored(kUiDanger, "| will not play");
    ImGui::TextWrapped("%s", sceneKindSummary(report.kind));

    ImGui::Spacing();
    ImGui::SeparatorText("shape");

    // Changing the shape is one click, and it keeps the camera where the author
    // put it (setSceneView). This was four manual component edits.
    //
    // Expressed as an edit or a create rather than a whole-document swap: the
    // operation touches exactly one entity, and the undo stack should say so.
    const auto setView = [this, &report](SceneKind kind, const char* label) {
        const bool current = report.kind == kind;
        if (current)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_Header));
        const bool clicked = ImGui::Button(label);
        if (current)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n\nThe camera entity is reused, keeping its "
                              "transform and its name.",
                              sceneKindSummary(kind));
        }
        if (!clicked)
            return;
        Doc after = mState.document;
        const AuthorId id = setSceneView(after, kind);
        if (id.empty())
            return;
        const Entity* fixed = after.find(id);
        if (!fixed)
            return;
        const std::string label2 =
            std::string("make ") + sceneKindName(kind) + " scene";
        if (const Entity* before = mState.document.find(id))
            runCommand(makeEditEntity(label2, id, *before, *fixed));
        else
            runCommand(makeCreateEntity(*fixed));
        // The kind decides which editor the scene wants. Making a level flat
        // and staying in the 3D view would show the page edge-on.
        setMainScreen(mainScreenForKind(kind));
    };
    // Wrapped rather than run off the edge: four buttons is more than a
    // 296-pixel dock holds, and "2D screen" disappearing is the one that turns
    // a menu scene into an unauthorable one.
    setView(SceneKind::FirstPerson, "First person");
    ui::sameLineIfItFits(ui::buttonWidth("Third person"));
    setView(SceneKind::ThirdPerson, "Third person");
    ui::sameLineIfItFits(ui::buttonWidth("Shot"));
    setView(SceneKind::Shot, "Shot");
    ui::sameLineIfItFits(ui::buttonWidth("2D screen"));
    setView(SceneKind::Screen, "2D screen");

    ImGui::Spacing();
    ImGui::SeparatorText("roles");

    // Rows, not a table.
    //
    // This was a four-column table and it did not survive its own dock: at the
    // Inspector rail's default width the detail column got about ninety pixels,
    // and TextWrapped in ninety pixels renders a sentence one character per
    // line. The panel was a vertical alphabet. Nothing about a role is tabular
    // -- there is no column an author scans down -- so each one is now a block
    // that reflows at any width.
    for (const RoleStatus& status : report.roles) {
        // A role this kind of scene does not have is not a hole. Reporting "no
        // player spawn" on a menu is how a checklist gets ignored.
        if (!status.applicable)
            continue;
        ImGui::PushID(int(status.role));

        const bool hole = status.count == 0 && status.severity != Severity::Info;
        const ImVec4 nameColour =
            hole ? (status.severity == Severity::Error ? kUiDanger : kUiWarning)
                 : ImGui::GetStyleColorVec4(ImGuiCol_Text);

        // The name and what fills it, on one line. The fix button is pushed to
        // the right of that same line rather than given a column, so a role
        // with nothing to fix costs no width at all.
        ImGui::TextColored(nameColour, "%s", sceneRoleName(status.role));
        if (status.fix != QuickFix::None) {
            const float button = ui::buttonWidth("Add");
            const float at = ImGui::GetContentRegionMax().x - button;
            if (at > ImGui::GetCursorPosX()) {
                ImGui::SameLine();
                ImGui::SetCursorPosX(at);
            }
            else {
                ImGui::SameLine();
            }
            // The button the docs promised. "Every unfilled required role
            // carries a QuickFix" was true of the data and not of the panel:
            // the fix reached the Problems list and never this table, so the
            // one surface that named the hole could not close it.
            if (ImGui::SmallButton("Add")) {
                Issue synthetic;
                synthetic.severity = status.severity;
                synthetic.code = "scene.role_unfilled";
                synthetic.message = status.detail;
                synthetic.entity = status.filledBy;
                synthetic.fix = status.fix;
                Doc after = mState.document;
                if (applyQuickFix(after, mState.catalog, synthetic)) {
                    runCommand(Command{
                        std::string("fill ") + sceneRoleName(status.role),
                        [after](Doc& doc) { doc = after; },
                        [before = mState.document](Doc& doc) { doc = before; }});
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("add what fills this role");
        }

        ImGui::Indent();
        // Clicking the filler selects it: the fastest possible answer to
        // "which entity is the camera in this scene".
        if (!status.filledBy.empty()) {
            if (ImGui::Selectable(status.filledBy.c_str(),
                                  mState.isSelected(status.filledBy)))
                selectAndReveal(status.filledBy, false);
        }
        else {
            ImGui::TextDisabled("--");
        }
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", status.detail.c_str());
        ImGui::PopStyleColor();
        ImGui::Unindent();
        ImGui::Spacing();
        ImGui::PopID();
    }

    // Two views is legal -- a debug or death cam takes over by existing at a
    // higher priority -- so it is reported rather than refused.
    if (report.views.size() > 1) {
        ImGui::Spacing();
        ImGui::TextColored(kUiWarning, "%zu entities carry a view",
                           report.views.size());
        for (const AuthorId& id : report.views) {
            ImGui::Bullet();
            ImGui::SameLine();
            if (ImGui::SmallButton(id.c_str()))
                selectAndReveal(id, false);
        }
        ImGui::TextDisabled("legal: the higher Camera priority wins.");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("environment");
    // The palette is the level's, not the session's: a crypt and a cathedral
    // are not the same room with different props. It lives here rather than in
    // the Inspector's nothing-selected state because the contract lists
    // Environment as a role, and the role and the control that fills it
    // belonging to two different panels is how the role went unnoticed.
    const std::string current = mState.document.palette.empty()
                                    ? std::string("(the game's default)")
                                    : mState.document.palette;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##palette", current.c_str())) {
        if (ImGui::Selectable("(the game's default)",
                              mState.document.palette.empty()))
            setScenePalette({});
        for (const std::string& name : mPalettes) {
            if (ImGui::Selectable(name.c_str(),
                                  name == mState.document.palette))
                setScenePalette(name);
        }
        ImGui::EndCombo();
    }
    if (!mState.document.palette.empty() &&
        std::find(mPalettes.begin(), mPalettes.end(),
                  mState.document.palette) == mPalettes.end()) {
        ImGui::TextColored(kUiDanger, "'%s' is not in palettes.toml",
                           mState.document.palette.c_str());
    }
    ImGui::TextDisabled("View > Scene lighting shows it in the viewport.");

    ImGui::Spacing();
    ImGui::SeparatorText("this scene");
    ImGui::TextDisabled("id        %s", mState.document.id.c_str());
    ImGui::TextDisabled("entities  %zu", mState.document.entities.size());
    ImGui::TextDisabled("file      %s",
                        mState.scenePath.empty() ? "not saved yet"
                                                 : mState.scenePath.c_str());
    ImGui::TextDisabled("cook      %s", mCookStatus.c_str());

    ImGui::End();
}

// The undo stack, as a list. Godot's History dock.
//
// Undo had two surfaces before this -- a menu item and a keybind -- and both
// answered "step back one" without ever saying what the steps were. A blockout
// session is two hundred of them, and "which of these was the one where I
// deleted the corridor" is not a question Ctrl+Z answers.
void EditorApp::drawHistory()
{
    focusPanelIfRequested("history");
    if (!ImGui::Begin(workspace_window::kHistory, nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }

    const std::vector<std::string> done = mCommands.doneLabels();
    const std::vector<std::string> undone = mCommands.undoneLabels();

    ImGui::BeginDisabled(!mCommands.canUndo());
    if (ImGui::Button("Undo"))
        applyHistory(false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!mCommands.canRedo());
    if (ImGui::Button("Redo"))
        applyHistory(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu of %zu", done.size(), done.size() + undone.size());
    ImGui::Separator();

    if (ImGui::BeginChild("##history_list")) {
        std::size_t walkTo = ~std::size_t(0);

        // Undone entries first and greyed: they are the future, drawn above the
        // present the same way the stack is drawn in every editor that has one.
        for (std::size_t i = undone.size(); i-- > 0;) {
            ImGui::PushID(int(1000 + i));
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImGui::GetStyleColorVec4(
                                      ImGuiCol_TextDisabled));
            if (ImGui::Selectable(undone[i].c_str()))
                walkTo = done.size() + (undone.size() - i);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }

        // The line the document is currently at. Named rather than drawn as a
        // bare separator, because "you are here" is the only thing in this
        // panel that is not a command.
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
        ImGui::TextUnformatted("--- now ---");
        ImGui::PopStyleColor();

        for (std::size_t i = done.size(); i-- > 0;) {
            ImGui::PushID(int(i));
            if (ImGui::Selectable(done[i].c_str()))
                walkTo = i + 1; // clicking a row leaves that command applied
            ImGui::PopID();
        }
        if (done.empty()) {
            if (ImGui::Selectable("(the scene as it opened)"))
                walkTo = 0;
        }
        else if (ImGui::Selectable("(the scene as it opened)")) {
            walkTo = 0;
        }

        if (walkTo != ~std::size_t(0)) {
            const std::size_t steps = mCommands.walkTo(mState.document, walkTo);
            if (steps > 0) {
                // The same bookkeeping applyHistory does, and for the same
                // reason: an undone placement that stays on screen until the
                // next unrelated edit is the bug three copies of this code once
                // had between them.
                mState.dirty = !mCommands.savedStateReached();
                mState.document.touch();
                finishInspectorEdit();
                if (mPreview)
                    mPreview->invalidate();
                mStatus = "walked " + std::to_string(steps) +
                          (steps == 1 ? " step" : " steps") + " of history";
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// The validator's output, in the bottom panel.
//
// It used to open with the scene contract -- what the scene IS -- on the
// argument that "what does this do" comes before "what is wrong with it". That
// argument was right and the placement was wrong: nobody opens a Problems tab
// to find out what a scene is for. The contract is a dock of its own now, next
// to the Inspector, and this is what it always should have been: a list of what
// is broken, and the buttons that fix it.
void EditorApp::drawIssues()
{
    {
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
        ImGui::SameLine();
        if (ImGui::SmallButton("Contract"))
            requestPanelFocus("contract");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("what this scene IS -- its kind, and the roles "
                              "it fills");
        ImGui::Separator();

        if (ImGui::BeginChild("##issues")) {
            if (mIssues.empty())
                ImGui::TextDisabled("No issues -- this scene can cook.");
            if (ImGui::BeginTable("##issue_table", 4,
                                  ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn(
                    "Severity", ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableSetupColumn(
                    "Issue", ImGuiTableColumnFlags_WidthFixed, 116.0f);
                ImGui::TableSetupColumn("Message",
                                        ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(
                    "Action", ImGuiTableColumnFlags_WidthFixed, 112.0f);
                ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < mIssues.size(); ++i) {
                    const Issue issue = mIssues[i];
                    const ImVec4 colour = issue.severity == Severity::Error
                                              ? kUiDanger
                                              : kUiWarning;
                    ImGui::PushID(int(i));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(colour, "%s",
                                       severityName(issue.severity));
                    ImGui::TableNextColumn();
                    const bool selected = !issue.entity.empty() &&
                                          mState.isSelected(issue.entity);
                    if (ImGui::Selectable(
                            issue.code.c_str(), selected,
                            ImGuiSelectableFlags_AllowDoubleClick) &&
                        !issue.entity.empty()) {
                        selectAndReveal(issue.entity, false);
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            frameSelectionOrAll();
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", issue.message.c_str());
                    ImGui::TableNextColumn();
                    if (issue.fix != QuickFix::None) {
                        const auto fixLabel = [](QuickFix fix) {
                            switch (fix) {
                            case QuickFix::RemoveEntity:
                                return "Remove...";
                            case QuickFix::AddPlayerSpawn:
                                return "Add spawn";
                            case QuickFix::SetDefaultRange:
                                return "Set range";
                            case QuickFix::SetDefaultHalfExtents:
                                return "Set extents";
                            case QuickFix::SnapToCell:
                                return "Snap to grid";
                            case QuickFix::ResetTransform:
                                return "Reset transform";
                            case QuickFix::FillCornerGap:
                                return "Fill gap";
                            case QuickFix::ClearParent:
                                return "Detach";
                            case QuickFix::AddPortalComponent:
                                return "Add portal";
                            case QuickFix::AddFirstPersonView:
                                return "First person";
                            case QuickFix::AddThirdPersonView:
                                return "Third person";
                            case QuickFix::AddShotCamera:
                                return "Add camera";
                            case QuickFix::AddAudioListener:
                                return "Add listener";
                            case QuickFix::AddKeyLight:
                                return "Add key light";
                            case QuickFix::None:
                                break;
                            }
                            return "Apply";
                        };
                        const auto apply = [this, issue] {
                            const Entity* target =
                                issue.entity.empty()
                                    ? nullptr
                                    : mState.document.find(issue.entity);
                            Doc after = mState.document;
                            if (!applyQuickFix(after, mState.catalog, issue))
                                return;
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
                                for (const Entity& entity : after.entities)
                                    if (!mState.document.contains(entity.id))
                                        runCommand(makeCreateEntity(entity));
                            }
                            mPreview->invalidate();
                        };
                        if (ImGui::SmallButton(fixLabel(issue.fix))) {
                            if (issue.fix == QuickFix::RemoveEntity)
                                ConfirmDialog::open(
                                    "Remove the invalid entity?", issue.entity,
                                    apply, "Remove");
                            else
                                apply();
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }
}

// --- material staging mode ---------------------------------------------------

void EditorApp::setMode(ViewportMode mode)
{
    if (mState.mode == mode)
        return;
    // Leaving isolation is not optional when another mode is entered: the two
    // both decide what the viewport draws, and a level hidden by one and
    // restored by the other is a viewport nobody can get back.
    if (mState.mode == ViewportMode::Isolate)
        leaveIsolation();
    if (mode == ViewportMode::Isolate) {
        // Entered through enterIsolation, which has an entity to isolate on.
        // Reaching here means a caller asked for the mode without one.
        return;
    }
    const bool material = mode == ViewportMode::Material;
    mState.mode = mode;
    if (material)
        mAssetBrowserModeRequest = 1; // Materials
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
    eng::Renderer& renderer = mEngine->renderer();

    // The swatch exists in both modes. Picking a material for a wall is the
    // common case, and it should not require leaving the level to see what the
    // material looks like.
    if (!mStage.thumbnailBuilt())
        mStage.buildThumbnail(renderer, 256);
    if (mMaterialNames.empty()) {
        mMaterialNames = renderer.materialNames();
        std::sort(mMaterialNames.begin(), mMaterialNames.end());
    }
    mStage.setThumbnailVisible(renderer, true);
    if (mParticleThumbnailNode.valid())
        renderer.setNodeVisible(mParticleThumbnailNode, false);

    // The preview follows the selection. Selecting a wall and looking at a
    // sphere wearing something else is the panel answering a question nobody
    // asked -- and it made "what is this pillar wearing" a trip through the
    // material list to find the name first.
    //
    // Only when the selection *changes*, so clicking a name in the list below
    // still previews that name: the author is then asking about the material,
    // not about the entity.
    const AuthorId* previewOf = mState.primary();
    const std::string selectedId = previewOf ? *previewOf : std::string();
    if (selectedId != mPreviewedEntity) {
        mPreviewedEntity = selectedId;
        if (const Entity* e =
                previewOf ? mState.document.find(*previewOf) : nullptr) {
            // The entity's override, else what its kit piece wears. An entity
            // with neither has nothing to say and the swatch keeps its last
            // subject rather than blanking.
            std::string worn = e->material;
            if (worn.empty()) {
                if (const KitPiece* piece = mState.catalog.find(e->prefab))
                    worn = piece->material;
            }
            if (!worn.empty()) {
                mSelectedMaterial = worn;
                mStage.setThumbnailMaterial(renderer, worn);
            }
        }
    }

    const std::string& previewMaterial = mStage.thumbnailMaterial().empty()
                                             ? mSelectedMaterial
                                             : mStage.thumbnailMaterial();
    // What the selection would take, so the list can say what will happen
    // before the click rather than after the cook.
    const MeshKind targetMesh = selectionMeshKind();

    ed::ui::AssetPanelView view;
    view.previewTexture = renderer.materialThumbnailTextureId();
    view.previewTooltip = "Drag to turn the sphere. Hovering a row previews it.";
    view.onPreviewDrag = [&](float dx) {
        mThumbAutoSpin = false;
        mStage.spinThumbnail(renderer, mStage.thumbnailSpin() + dx * 0.01f);
    };

    view.metadata = [&] {
        ImGui::TextUnformatted(previewMaterial.empty() ? "(no material)"
                                                       : previewMaterial.c_str());
        ImGui::TextDisabled("preview: %s",
                            mStage.thumbnailPreviewMode() == StagePreview::Quad
                                ? "animated quad"
                                : "lit sphere");
        if (const MaterialInfo* info = materialInfo(previewMaterial)) {
            ImGui::TextDisabled("%s%s", materialClassName(info->klass),
                                info->twoSided ? " | two-sided" : "");
            if (!info->texture.empty())
                ImGui::TextDisabled("texture: %s", info->texture.c_str());
        }
    };

    view.actions = [&] {
        // Applying to the selection is the reason the panel exists in scene
        // mode.
        const AuthorId* primary = mState.primary();
        bool canApply = primary != nullptr && !mSelectedMaterial.empty();
        std::string cannotApply;
        if (const MaterialInfo* info = materialInfo(mSelectedMaterial)) {
            const MaterialAdvice advice = materialFits(info->klass, targetMesh);
            if (advice.fit == Fit::Broken) {
                canApply = false;
                cannotApply = advice.reason;
            }
        }
        ImGui::BeginDisabled(!canApply);
        if (ImGui::Button("Apply to Selection"))
            applyMaterialToSelection(mSelectedMaterial);
        ImGui::EndDisabled();
        if (!canApply && !cannotApply.empty() &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Cannot apply: %s", cannotApply.c_str());
    };

    view.toggles = [&] {
        ImGui::Checkbox("spin", &mThumbAutoSpin);
        ImGui::SameLine();
        bool material = materialMode();
        if (ImGui::Checkbox("Material Stage", &material))
            setMode(material ? ViewportMode::Material : ViewportMode::Level);
        ImGui::SameLine();
        eng::imguihint::marker(
            "editor.staging_mode",
            "Shows one material on a sphere over a reference floor, lit by the "
            "game's own shaders -- not a PBR preview, because the game does not "
            "render PBR. What you see here is what the dungeon will show.");

        // Most of the shipped catalogue cannot go on an entity at all:
        // compositor passes, particle materials, sprite and decal materials.
        // Listing them beside the ones an author is choosing between is how a
        // bloom pass ends up on a wall.
        ImGui::Checkbox("show non-surface materials", &mShowAllMaterials);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "particle, sprite and compositor materials -- they "
                "need geometry the engine generates, not an entity's");

        if (materialMode()) {
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
                mState.camera.setYawPitch(mStage.cameraYaw(),
                                          mStage.cameraPitch());
            }
        }
    };

    view.filter = mMaterialFilter;
    view.filterCapacity = sizeof(mMaterialFilter);
    view.filterHint = "Search materials...";
    view.footer = std::to_string(mMaterialNames.size()) + " materials";

    bool hoveredMaterial = false;
    view.list = [&] {
        const std::string filter = mMaterialFilter;
        MaterialClass section = MaterialClass::Unknown;
        bool first = true;
        for (const MaterialInfo& info : materialCatalog()) {
            const std::string& name = info.name;
            if (!ed::ui::filterMatches(name, filter))
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
                ImGui::PushStyleColor(ImGuiCol_Text, advice.fit == Fit::Broken
                                                         ? kUiDanger
                                                         : kUiWarning);
            }
            const std::string label =
                eng::assets::friendlyAssetLabel(name) + "###" + name;
            const bool picked =
                ImGui::Selectable(label.c_str(), name == mSelectedMaterial);
            if (warn)
                ImGui::PopStyleColor();

            if (picked) {
                mSelectedMaterial = name;
                mStage.setThumbnailMaterial(renderer, name);
                if (materialMode())
                    mStage.setMaterial(renderer, name);
            }
            // Hovering previews. Selecting commits. That split is what makes
            // scrubbing a long list to find the right material actually work.
            if (ImGui::IsItemHovered()) {
                hoveredMaterial = true;
                mStage.setThumbnailMaterial(renderer, name);
                if (warn && !advice.reason.empty()) {
                    ImGui::SetTooltip("%s\n\n%s\n%s", name.c_str(),
                                      advice.fit == Fit::Broken
                                          ? "Will not render on this selection:"
                                          : "Will render, but probably wrong:",
                                      advice.reason.c_str());
                }
                else {
                    ImGui::SetTooltip("%s\n%s%s%s", name.c_str(),
                                      info.texture.empty()
                                          ? "no texture"
                                          : info.texture.c_str(),
                                      info.shader.empty() ? "" : "  |  ",
                                      info.shader.c_str());
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
    };

    ed::ui::drawAssetPanel(view);

    // Hover is a temporary comparison, not a selection. Leaving the list must
    // restore the named swatch; otherwise its image and title disagree until
    // another row is clicked.
    if (!hoveredMaterial && !mSelectedMaterial.empty() &&
        mStage.thumbnailMaterial() != mSelectedMaterial)
        mStage.setThumbnailMaterial(renderer, mSelectedMaterial);
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
        std::stable_sort(mMaterialCatalog.begin(), mMaterialCatalog.end(),
                         [](const MaterialInfo& a, const MaterialInfo& b) {
                             if (a.klass != b.klass)
                                 return static_cast<int>(a.klass) <
                                        static_cast<int>(b.klass);
                             return a.name < b.name;
                         });
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
        if (!entity)
            continue;
        // A generated primitive states its own UVs: the generators lay 0..1
        // over each face, which is exactly what Generated means. No kit piece
        // to ask, and no guessing -- this is the one case the answer is certain.
        MeshKind one = MeshKind::Unknown;
        if (entity->primitive) {
            one = MeshKind::Generated;
        }
        else if (entity->mesh) {
            // A mesh file authored outside the kit has ordinary wrapping UVs
            // until something says otherwise. Tiling is the honest default:
            // it is what an imported model has, and the advice it produces
            // ("this atlas material will smear") is the one worth giving.
            one = MeshKind::Tiling;
        }
        else if (!entity->prefab.empty()) {
            const KitPiece* piece = mState.catalog.find(entity->prefab);
            if (!piece)
                continue;
            const MaterialInfo* info = materialInfo(piece->material);
            one = info ? meshKindForMaterial(info->klass) : MeshKind::Unknown;
        }
        else {
            continue;
        }
        if (!any) {
            kind = one;
            any = true;
        }
        else if (kind != one) {
            return MeshKind::Unknown;
        }
    }
    return kind;
}

// Every mesh file in the project, scanned on first use and then kept.
//
// Two hundred files, one stat() each: fast enough to do lazily and far too slow
// to do at startup for a panel that may never be opened. Cross-referenced with
// the kit so a row can say "this is already kit.wall" -- the same crossing the
// material catalogue does against the renderer, and for the same reason: a list
// that offers the worse of two routes without saying so is a trap.
const MeshCatalog& EditorApp::meshCatalog()
{
    if (!mMeshCatalog.loaded()) {
        std::vector<std::string> extensions;
        for (const std::string& ext : eng::Renderer::supportedModelExtensions())
            extensions.push_back(ext.front() == '.' ? ext : "." + ext);
        mMeshCatalog.load(eng::assets::resolve("meshes").string(), extensions);

        std::vector<std::pair<std::string, std::string>> prefabs;
        std::vector<std::pair<std::string, std::string>> materials;
        for (const KitPiece& piece : mState.catalog.all()) {
            // A group piece names no mesh, and an entry keyed on "" would tell
            // every mesh in the catalogue it is that group.
            if (piece.isGroup())
                continue;
            prefabs.emplace_back(piece.meshPath, piece.id);
            materials.emplace_back(piece.meshPath, piece.material);
        }
        mMeshCatalog.annotate(prefabs, materials);
        eng::log::info("Editor: %zu meshes catalogued",
                       mMeshCatalog.all().size());
    }
    return mMeshCatalog;
}

void EditorApp::requestMeshPreview(const std::string& meshPath,
                                   const std::string& material)
{
    if (!mEngine)
        return;
    eng::Renderer& renderer = mEngine->renderer();
    if (!mStage.thumbnailBuilt())
        mStage.buildThumbnail(renderer, 256);

    const std::string subject = meshPath + "|" + material;
    if (mPreviewSubject == PreviewSubject::Mesh && mMeshPreviewName == subject)
        return;
    mPreviewSubject = PreviewSubject::Mesh;
    mMeshPreviewName = subject;

    // Cached across frames: hovering down a list re-enters this every frame,
    // and re-parsing an OBJ each time is the difference between a browsable
    // list and an unusable one.
    auto cached = mMeshPreviewCache.find(meshPath);
    if (cached == mMeshPreviewCache.end()) {
        const std::filesystem::path full = eng::assets::resolve(meshPath);
        eng::ModelImportOptions options;
        options.pivot = eng::PivotMode::Source;
        const eng::MeshHandle mesh =
            full.empty() ? renderer.prototypeMesh(meshPath)
                         : renderer.loadMesh(full.string(), options);
        cached = mMeshPreviewCache.emplace(meshPath, mesh).first;
    }
    // A material the mesh was never authored against says more about the
    // material than the mesh, so the kit's own is preferred and the fallback is
    // a neutral surface rather than whatever the last material row left behind.
    mStage.setThumbnailMesh(renderer, cached->second,
                            material.empty() ? std::string("Game/Kit/Dungeon")
                                             : material);
}

void EditorApp::requestPrimitivePreview(const eng::ecs::PrimitiveMesh& primitive)
{
    if (!mEngine)
        return;
    eng::Renderer& renderer = mEngine->renderer();
    if (!mStage.thumbnailBuilt())
        mStage.buildThumbnail(renderer, 256);

    // Keyed on the parameters, through the same string the ghost cache uses:
    // two boxes of different sizes are two subjects, and a two-metre pillar
    // previewed as the last capsule teaches nothing.
    const std::string subject = primitiveGhostKey(primitive);
    if (mPreviewSubject == PreviewSubject::Mesh && mMeshPreviewName == subject)
        return;
    mPreviewSubject = PreviewSubject::Mesh;
    mMeshPreviewName = subject;
    mStage.setThumbnailMesh(renderer,
                            mPrimitivePreviewMeshes.get(renderer, primitive),
                            "Game/Kit/Dungeon");
}

void EditorApp::useMeshBrush(const std::string& meshPath)
{
    leaveMaterialStage();
    mState.brush.kind = Brush::Kind::Mesh;
    mState.brush.meshPath = meshPath;
    mState.tool = Tool::Place;
    mStatus = "mesh brush: " + meshPath +
              " -- move over Viewport to preview, click to place";
}

void EditorApp::usePrimitiveBrush(const eng::ecs::PrimitiveMesh& primitive)
{
    leaveMaterialStage();
    mState.brush.kind = Brush::Kind::Primitive;
    mState.brush.primitive = primitive;
    mState.tool = Tool::Place;
    mStatus = std::string("primitive brush: ") +
              eng::ecs::primitiveKindName(primitive.kind) +
              " -- move over Viewport to preview, click to place";
}

// Swapping what the selection is made of, as one undo entry.
//
// Replacing rather than adding: the three ways to be a mesh are exclusive, and
// an entity that ended up with two would be refused by its own file format on
// the next load. The material is cleared with the old geometry because a
// material is chosen for a mesh -- a wall's atlas coordinates mean nothing on
// a generated sphere.
void EditorApp::applyMeshToSelection(const std::string& meshPath)
{
    if (meshPath.empty() || mState.selection.empty())
        return;
    std::vector<Command> parts;
    for (const AuthorId& id : mState.selection) {
        const Entity* entity = mState.document.find(id);
        if (!entity)
            continue;
        Entity after = *entity;
        after.prefab.clear();
        after.cell.reset();
        after.primitive.reset();
        after.mesh = game::content::MeshAuthor{meshPath, 1.0f};
        if (const MeshAsset* asset = meshCatalog().find(meshPath))
            after.material = asset->material;
        parts.push_back(makeEditEntity("mesh", id, *entity, after));
    }
    if (parts.empty())
        return;
    runCommand(makeComposite("set mesh", std::move(parts)));
    mPreview->invalidate();
    mStatus = "set mesh " + meshPath + " on " +
              std::to_string(mState.selection.size()) + " entity(s)";
}

void EditorApp::applyPrimitiveToSelection(
    const eng::ecs::PrimitiveMesh& primitive)
{
    if (mState.selection.empty())
        return;
    std::vector<Command> parts;
    for (const AuthorId& id : mState.selection) {
        const Entity* entity = mState.document.find(id);
        if (!entity)
            continue;
        Entity after = *entity;
        after.prefab.clear();
        after.cell.reset();
        after.mesh.reset();
        after.primitive = primitive;
        parts.push_back(makeEditEntity("primitive", id, *entity, after));
    }
    if (parts.empty())
        return;
    runCommand(makeComposite("set primitive", std::move(parts)));
    mPreview->invalidate();
    mStatus = std::string("set primitive ") +
              eng::ecs::primitiveKindName(primitive.kind) + " on " +
              std::to_string(mState.selection.size()) + " entity(s)";
}

// Geometry facts about a mesh, taken from the LOADED mesh rather than the file:
// that is the geometry the game will actually draw, after the importer's
// welding, splitting and pivot handling.
void EditorApp::drawMeshGeometryInfo(const std::string& meshPath)
{
    const auto found = mMeshPreviewCache.find(meshPath);
    if (found == mMeshPreviewCache.end() || !found->second.valid())
        return;
    eng::Renderer& renderer = mEngine->renderer();
    eng::MeshBounds bounds;
    if (renderer.meshBounds(found->second, bounds)) {
        const glm::vec3 extent = bounds.max - bounds.min;
        ImGui::TextDisabled("%.2f x %.2f x %.2f m", double(extent.x),
                            double(extent.y), double(extent.z));
    }
    ImGui::TextDisabled("%zu submesh(es)",
                        renderer.meshSubmeshCount(found->second));
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
    eng::Renderer& renderer = mEngine->renderer();
    std::vector<eng::ParticleEffectDesc>& descs = mParticles.descs();

    if (descs.empty()) {
        ImGui::TextDisabled("no particles.toml loaded");
        return;
    }

    std::size_t selectedIndex = descs.size();
    for (std::size_t i = 0; i < descs.size(); ++i)
        if (descs[i].name == mSelectedEffect)
            selectedIndex = i;
    if (selectedIndex == descs.size()) {
        selectedIndex = 0;
        mSelectedEffect = descs.front().name;
    }

    // The material swatch RTT is a generic isolated square despite its legacy
    // name. Hide the material subject, put a real particle system at its camera
    // focus, and mark that system thumbnail-only so it cannot leak into the
    // scene viewport.
    if (!mStage.thumbnailBuilt())
        mStage.buildThumbnail(renderer, 256);
    mStage.setThumbnailVisible(renderer, false);
    if (!mParticleThumbnailNode.valid()) {
        mParticleThumbnailNode =
            renderer.createNode(eng::kRootNode, glm::vec3(0.0f, -1000.0f, 0.0f),
                                "particle_catalog_thumbnail");
    }
    renderer.setNodeVisible(mParticleThumbnailNode, true);

    eng::ParticleEffectDesc& d = descs[selectedIndex];
    bool dirty = false;

    const double now = ImGui::GetTime();
    const float period = particlePreviewPeriod(d);
    const bool thumbnailChanged =
        mParticleThumbnailEffect != d.name ||
        mParticleThumbnailScale != mParticlePreviewScale;
    const bool thumbnailExpired =
        period > 0.0f && now >= mParticleThumbnailRestartAt;
    if (thumbnailChanged || !mParticleThumbnail.valid() || thumbnailExpired) {
        if (mParticleThumbnail.valid())
            renderer.despawnParticles(mParticleThumbnail);
        eng::ParticleSpawnOptions options;
        options.sizeScale = mParticlePreviewScale;
        mParticleThumbnail =
            renderer.spawnParticles(d.name, mParticleThumbnailNode, options);
        renderer.setNodeThumbnailOnly(mParticleThumbnailNode, true);
        mParticleThumbnailEffect = d.name;
        mParticleThumbnailScale = mParticlePreviewScale;
        mParticleThumbnailRestartAt =
            period > 0.0f ? now + period : std::numeric_limits<double>::max();
    }

    const auto clearPreviews = [&] {
        for (eng::ParticlesHandle h : mParticlePreviews)
            renderer.despawnParticles(h);
        mParticlePreviews.clear();
    };

    ed::ui::AssetPanelView view;
    view.previewTexture = renderer.materialThumbnailTextureId();
    view.previewTooltip = "Live isolated preview. One-shot effects replay "
                          "automatically while this tab is visible.";

    view.metadata = [&] {
        ImGui::TextUnformatted(eng::assets::friendlyAssetLabel(d.name).c_str());
        ImGui::TextDisabled("%s", d.name.c_str());
        ImGui::TextDisabled(
            "%s | %s | %zu emitter(s)",
            d.renderMode == eng::ParticleRenderMode::Voxel ? "voxel" : "sprite",
            d.loop ? "loop" : "burst", d.emitters.size());
        ImGui::TextDisabled(
            "focus %.1f, %.1f, %.1f", double(mState.camera.target().x),
            double(mState.camera.target().y), double(mState.camera.target().z));
    };

    view.actions = [&] {
        // Spawning at camera focus rather than world origin makes selection
        // work like the material swatch: the result is immediately in the view
        // being worked in, and selection restarts one clean preview instead of
        // piling up.
        if (ImGui::Button("Preview at Focus")) {
            clearPreviews();
            eng::ParticleSpawnOptions options;
            options.sizeScale = mParticlePreviewScale;
            const eng::ParticlesHandle h = renderer.spawnParticles(
                d.name, mState.camera.target(), options);
            if (h.valid()) {
                mParticlePreviews.push_back(h);
                mStatus = "previewing " + d.name + " at the viewport focus";
            }
            else {
                mStatus = "could not preview " + d.name;
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(mParticlePreviews.empty());
        if (ImGui::Button("Clear Preview"))
            clearPreviews();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Use as Brush")) {
            leaveMaterialStage();
            mState.brush.kind = Brush::Kind::Particles;
            mState.brush.effect = d.name;
            mState.tool = Tool::Place;
            mStatus = "particle brush: " + d.name +
                      " -- move over Viewport to preview, click to place";
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Places a ParticleEmitter entity; the viewport brush "
                "shows the real live effect before the click");
        if (ImGui::Button("Save Effects")) {
            if (mParticles.save(mParticles.path())) {
                mParticlesDirty = false;
                mStatus = "saved " + mParticles.path();
            }
            else {
                mStatus = "could not write " + mParticles.path();
            }
        }
    };

    view.toggles = [&] {
        ImGui::SetNextItemWidth(
            std::min(180.0f, ImGui::GetContentRegionAvail().x));
        ImGui::SliderFloat("preview size", &mParticlePreviewScale, 0.05f, 8.0f,
                           "%.2fx");
        if (mParticlesDirty)
            ImGui::TextColored(kUiWarning, "Unsaved particle changes");
    };

    view.filter = mParticleFilter;
    view.filterCapacity = sizeof(mParticleFilter);
    view.filterHint = "Search effects...";
    view.footer = std::to_string(descs.size()) + " effects  |  " +
                  std::filesystem::path(mParticles.path()).filename().string();

    view.list = [&] {
        const std::string filter = mParticleFilter;
        for (int i = 0; i < int(descs.size()); ++i) {
            eng::ParticleEffectDesc& effect = descs[std::size_t(i)];
            if (!ed::ui::filterMatches(effect.name, filter))
                continue;
            const std::string label =
                eng::assets::friendlyAssetLabel(effect.name) + "###" +
                effect.name;
            if (ImGui::Selectable(label.c_str(),
                                  mSelectedEffect == effect.name))
                mSelectedEffect = effect.name;
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\n%s | %zu emitter(s) | %s", effect.name.c_str(),
                    effect.renderMode == eng::ParticleRenderMode::Voxel
                        ? "voxel"
                        : "sprite",
                    effect.emitters.size(), effect.loop ? "loop" : "burst");
            }
        }
    };

    ed::ui::drawAssetPanel(view);

    // The selected effect's own parameters, below the shared layout. Every tab
    // shares the top half -- preview, metadata, toggles, list -- and Effects is
    // the one that also *edits* its subject, so the editor hangs under it
    // rather than displacing the arrangement the other three establish.
    if (!ImGui::CollapsingHeader("tune this effect",
                                 ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::SeparatorText("appearance");

    // --- presentation ------------------------------------------------------
    const char* textureLabel =
        d.texture.empty() ? "(material only)" : d.texture.c_str();
    if (ImGui::BeginCombo("Texture", textureLabel)) {
        if (ImGui::Selectable("(material only)", d.texture.empty())) {
            d.texture.clear();
            dirty = true;
        }
        for (const eng::ParticleTextureDesc& texture :
             renderer.particleTextures()) {
            if (ImGui::Selectable(texture.stem.c_str(),
                                  d.texture == texture.stem)) {
                d.texture = texture.stem;
                dirty = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s | %d frame(s) at %.1f fps | %s",
                                  texture.file.empty() ? texture.stem.c_str()
                                                       : texture.file.c_str(),
                                  texture.flipbook.frameCount(),
                                  double(texture.flipbook.fps),
                                  texture.nearest ? "nearest" : "linear");
            }
        }
        ImGui::EndCombo();
    }
    if (d.texture.empty()) {
        char material[128] = {};
        std::snprintf(material, sizeof(material), "%s", d.material.c_str());
        if (ImGui::InputTextWithHint("Material", "Engine/Particles/...",
                                     material, sizeof(material))) {
            d.material = material;
            dirty = true;
        }
    }
    dirty |= ImGui::DragFloat("Width (m)", &d.baseWidth, 0.005f, 0.001f, 4.0f);
    dirty |=
        ImGui::DragFloat("Height (m)", &d.baseHeight, 0.005f, 0.001f, 4.0f);
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

    ImGui::SeparatorText("motion");
    // --- motion ------------------------------------------------------------
    dirty |=
        ImGui::DragFloat3("Acceleration (m/s^2)", &d.acceleration.x, 0.05f);
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
            dirty |= ImGui::SliderFloat("Angle (deg)", &em.angleDegrees, 0.0f,
                                        180.0f);
            if (d.loop)
                dirty |= ImGui::DragFloat("Rate (/s)", &em.emissionRate, 1.0f,
                                          0.0f, 4096.0f);
            dirty |= ImGui::DragFloatRange2("TTL (s)", &em.ttlMin, &em.ttlMax,
                                            0.01f, 0.001f, 60.0f);
            dirty |=
                ImGui::DragFloatRange2("Velocity (m/s)", &em.velocityMin,
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
    if (dirty) {
        mParticlesDirty = true;
        mParticles.reregister(renderer, selectedIndex);
    }
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
    if (mOpenSceneOpen && !ImGui::IsPopupOpen(kTitle)) {
        mOpenSceneEntries = listScenes(mState.assetRoot + "/scenes");
        mOpenSceneSelection.clear();
        ImGui::OpenPopup(kTitle);
    }
    const ImVec2 work = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(
        ImVec2(std::min(560.0f, std::max(280.0f, work.x - 32.0f)),
               std::min(420.0f, std::max(240.0f, work.y - 64.0f))),
        ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kTitle, nullptr,
                                ImGuiWindowFlags_NoSavedSettings))
        return;

    const std::string directory = mState.assetRoot + "/scenes";
    ImGui::TextDisabled("%s", directory.c_str());
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##openfilter", "Filter scenes", mOpenFilter,
                             sizeof(mOpenFilter));
    if (ImGui::SmallButton("Refresh"))
        mOpenSceneEntries = listScenes(directory);
    ImGui::SameLine();
    ImGui::TextDisabled("single-click selects; double-click or Enter opens");

    const std::vector<SceneEntry> entries =
        filterScenes(mOpenSceneEntries, mOpenFilter);
    std::string activate;

    if (ImGui::BeginChild("##scenes",
                          ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()))) {
        if (!mRecent.paths().empty() && mOpenFilter[0] == '\0') {
            ImGui::SeparatorText("recent");
            for (const std::string& path : mRecent.paths()) {
                const std::string name =
                    std::filesystem::path(path).filename().string();
                ImGui::PushID(path.c_str());
                const bool current = path == mState.scenePath;
                ImGui::BeginDisabled(current);
                if (ImGui::Selectable(name.c_str(), mOpenSceneSelection == path,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    mOpenSceneSelection = path;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        activate = path;
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", path.c_str());
                ImGui::PopID();
            }
        }
        ImGui::SeparatorText("scenes");
        if (entries.empty()) {
            ImGui::TextDisabled(
                mOpenFilter[0] ? "nothing matches"
                               : "no scenes here yet -- File > New, then Save "
                                 "as...");
        }
        for (const SceneEntry& entry : entries) {
            ImGui::PushID(entry.path.c_str());
            const bool current = entry.path == mState.scenePath;
            ImGui::BeginDisabled(current);
            if (ImGui::Selectable(entry.name.c_str(),
                                  mOpenSceneSelection == entry.path,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                mOpenSceneSelection = entry.path;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    activate = entry.path;
            }
            ImGui::EndDisabled();
            if (current) {
                ImGui::SameLine();
                ImGui::TextDisabled("(open)");
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                       ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    ImGui::BeginDisabled(mOpenSceneSelection.empty());
    if (ImGui::Button("Open", ImVec2(ui::dialogButtonWidth(), 0.0f)) || enter)
        activate = mOpenSceneSelection;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(ui::dialogButtonWidth(), 0.0f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        mOpenSceneOpen = false;
        ImGui::CloseCurrentPopup();
    }
    if (!activate.empty()) {
        ImGui::CloseCurrentPopup();
        requestOpen(activate);
    }
    if (!mOpenSceneOpen)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Persistent preferences only. Live viewport and playtest controls have one
// home in Window and Play; duplicating them here produced two control surfaces
// whose displayed state could drift within one frame.
void EditorApp::drawSettings()
{
    if (!mSettingsOpen)
        return;
    ui::centreNextModal(34.0f, 37.0f, 24.0f, 21.0f);
    if (!ImGui::Begin("Settings", &mSettingsOpen, ImGuiWindowFlags_NoDocking)) {
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
        "<name>.autosave.scn while there is unsaved work. File > Recover "
        "autosave reads it back.");
    ImGui::Spacing();

    changed |=
        ImGui::Checkbox("Back up automatically", &mSettings.autosaveEnabled);

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
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("F5 saves the scene itself before it cooks, so a "
                       "playtest needs no backup of its own.");
    ImGui::PopStyleColor();
    ImGui::EndDisabled();

    ImGui::Spacing();
    // What the setting is doing right now. A backup schedule you cannot see is
    // a backup schedule you have to take on faith.
    if (!mSettings.autosaveEnabled) {
        ImGui::TextColored(kUiWarning,
                           "off -- unsaved work is not being backed up");
    }
    else if (!mState.dirty) {
        ImGui::TextDisabled(
            "nothing unsaved; the clock starts at the next edit");
    }
    else {
        ImGui::Text("next backup in %d:%02d", int(mAutosaveIn) / 60,
                    int(mAutosaveIn) % 60);
    }
    const std::string backup =
        autosavePath(mState.scenePath, mState.assetRoot + "/scenes");
    // Wrapped, not truncated: the point of showing the path is that somebody
    // can go and find the file.
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", backup.empty() ? "(no path yet)" : backup.c_str());
    ImGui::PopStyleColor();

    ImGui::BeginDisabled(backup.empty());
    if (ImGui::Button("Back up now")) {
        if (writeAutosave()) {
            mAutosaveIn = mSettings.autosaveSeconds;
            mStatus = "wrote " + backup;
        }
        else {
            mStatus = "backup failed -- see the log for the filesystem error";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool canRecover = !backup.empty() && std::filesystem::exists(backup);
    ImGui::BeginDisabled(!canRecover);
    if (ImGui::Button("Recover autosave"))
        recoverAutosave();
    ImGui::EndDisabled();

    // --- interface ---------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Interface");

    float uiScale = mSettings.uiScale;
    if (ImGui::SliderFloat("Interface scale", &uiScale,
                           EditorSettings::kMinUiScale,
                           EditorSettings::kMaxUiScale, "%.2fx",
                           ImGuiSliderFlags_AlwaysClamp)) {
        mSettings.uiScale = uiScale;
        applyUiScale(uiScale);
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Scales tool text and controls together. Use a larger "
            "value for high-resolution displays or TV viewing.");

    // The chrome. Applied on the frame it changes rather than on restart: a
    // theme picker you have to relaunch to evaluate is a theme picker nobody
    // uses twice.
    if (ImGui::BeginCombo("Theme", mSettings.theme.c_str())) {
        for (const std::string& id : eng::imguitheme::ids()) {
            if (!ImGui::Selectable(id.c_str(), id == mSettings.theme))
                continue;
            mSettings.theme = id;
            eng::imguitheme::apply(id);
            // The theme resets the whole ImGuiStyle, including the sizes
            // ScaleAllSizes multiplied. Re-apply the scale or the interface
            // silently drops back to 1x.
            applyUiScale(mSettings.uiScale);
            changed = true;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("godot_dark is this editor's own: the shell is "
                          "Godot's arrangement, so the palette is too.\n"
                          "raven_editor is the engine's hard-edged chrome.");
    ImGui::TextDisabled(
        "Viewport state lives in the View menu and the viewport's own toolbar. "
        "Launch state lives in Debug. Those controls are saved when changed.");

    ImGui::Spacing();
    ImGui::SeparatorText("Where these live");
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", mSettingsFile.c_str());
    ImGui::PopStyleColor();
    ImGui::TextWrapped(
        "Per-user, like the recent-files list. The scene format, the key "
        "bindings and the window size are project content and stay in "
        "config/editor.toml.");
    if (ImGui::Button("Restore defaults")) {
        mSettings = EditorSettings{};
        applySettings();
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
    ui::centreNextModal(30.0f, 34.0f, 22.0f, 18.0f);
    if (!ImGui::Begin("Keyboard Shortcuts###Shortcuts", &mHelpOpen,
                      ImGuiWindowFlags_NoDocking)) {
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
    };
    static const Row kTools[] = {
        {"Q / P / B", "select / place / room mode"},
        {"W / E / R", "move / rotate / scale selection"},
        {"X", "gizmo axes: world or local"},
        {"G", "snap to grid"},
        {"[ / ]", "coarser / finer grid subdivision"},
        {"PgUp / PgDn / Home", "raise, lower, reset the work plane"},
    };
    // The Place tool's modifiers, which are the least discoverable thing in the
    // editor: nothing on screen suggests that Shift subtracts.
    static const Row kPlace[] = {
        {"wheel  (or , / .)", "rotate the brush a quarter turn"},
        {"drag", "paint -- one piece per cell crossed"},
        {"Shift+drag", "erase what the cursor crosses"},
        {"Alt+click", "eyedropper -- adopt the piece under the cursor"},
        {"Ctrl (held)", "ignore geometry, place on the work plane"},
        {"Esc (room drag)", "cancel the rectangle"},
    };
    static const Row kEdit[] = {
        {"Ctrl+Z", "undo"},
        {"Ctrl+Y / Ctrl+Shift+Z", "redo"},
        {"Ctrl+C / Ctrl+X", "copy / cut selection"},
        {"Ctrl+V", "paste, offset one cell"},
        {"Ctrl+D", "duplicate, offset one cell"},
        {"Delete", "delete selection"},
        {"Shift+click", "extend the selection"},
        {"Ctrl+Shift+A", "add a component in the Inspector"},
    };
    static const Row kScene[] = {
        {"Ctrl+P", "command palette -- every verb, by name"},
        {"Ctrl+1..6", "focus Scene, Assets, Hierarchy, Inspector, HUD, Problems"},
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
    section("place tool", kPlace, IM_ARRAYSIZE(kPlace));
    section("edit", kEdit, IM_ARRAYSIZE(kEdit));
    section("scene", kScene, IM_ARRAYSIZE(kScene));

    ImGui::Spacing();
    ImGui::TextWrapped(
        "Letter keys are mute while a text field has focus, and while the "
        "camera is flying -- W is both 'forward' and 'place tool'.");
    ImGui::End();
}

// Godot's Create New Scene, and the place the contract becomes the workflow
// rather than a report on it.
//
// A new scene needs two answers -- what it IS, and what it starts with -- and
// the editor used to ask only the second. "New > Empty" produced a document
// with no view component and no spawn, which loads, cooks, plays and shows
// nothing; docs/scenes.md calls that the failure the whole vocabulary exists to
// make loud, and the New menu was quietly manufacturing it.
void EditorApp::drawNewScenePopup()
{
    static constexpr const char* kTitle = "New scene";
    if (mNewSceneOpen && !ImGui::IsPopupOpen(kTitle)) {
        ImGui::OpenPopup(kTitle);
        mNewSceneOpen = false;
    }
    ed::ui::centreNextModal(34.0f, 26.0f, 26.0f, 18.0f);
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoSavedSettings))
        return;

    struct KindRow {
        SceneKind kind;
        const char* label;
    };
    // GameDriven first: it is what nearly every dungeon level is, and the one
    // whose name ("no authored camera") reads like a mistake until the summary
    // beside it explains that the game supplies one.
    static const KindRow kKinds[] = {
        {SceneKind::GameDriven, "Level"},
        {SceneKind::FirstPerson, "First person"},
        {SceneKind::ThirdPerson, "Third person"},
        {SceneKind::Shot, "Shot"},
        {SceneKind::Screen, "2D screen"},
    };
    static const SceneTemplate kTemplates[] = {
        SceneTemplate::Empty, SceneTemplate::Room, SceneTemplate::TechDemo,
        SceneTemplate::Screen};

    mNewSceneKind = std::clamp(mNewSceneKind, 0, int(std::size(kKinds)) - 1);
    mNewSceneTemplate =
        std::clamp(mNewSceneTemplate, 0, int(std::size(kTemplates)) - 1);

    ImGui::TextDisabled("What is this scene looked through?");
    ImGui::Spacing();
    for (int i = 0; i < int(std::size(kKinds)); ++i) {
        if (ImGui::RadioButton(kKinds[i].label, mNewSceneKind == i)) {
            mNewSceneKind = i;
            // Choosing the flat page picks its template too. The two are not
            // independent -- a Room template with a ScreenCamera over it is a
            // dungeon nobody can see -- and making the author discover that by
            // producing one is not a choice worth offering.
            if (kKinds[i].kind == SceneKind::Screen)
                mNewSceneTemplate = 3;
            else if (mNewSceneTemplate == 3)
                mNewSceneTemplate = 1;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("--");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", sceneKindSummary(kKinds[i].kind));
    }

    ImGui::Spacing();
    ImGui::SeparatorText("starting content");
    const bool screen = kKinds[mNewSceneKind].kind == SceneKind::Screen;
    ImGui::BeginDisabled(screen);
    for (int i = 0; i < int(std::size(kTemplates)); ++i) {
        // The page template only makes sense under the page kind, and vice
        // versa; neither is offered against the other.
        if ((kTemplates[i] == SceneTemplate::Screen) != screen)
            continue;
        if (ImGui::RadioButton(sceneTemplateName(kTemplates[i]),
                               mNewSceneTemplate == i))
            mNewSceneTemplate = i;
    }
    ImGui::EndDisabled();
    if (screen)
        ImGui::TextDisabled("a 2D screen always starts as a blank page");

    ImGui::Spacing();
    ImGui::Separator();
    const float width = ed::ui::dialogButtonWidth();
    if (ImGui::Button("Create", ImVec2(width, 0.0f))) {
        newSceneInTab(kTemplates[mNewSceneTemplate], kKinds[mNewSceneKind].kind);
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsWindowAppearing())
        ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(width, 0.0f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// New Project / Open Project.
//
// One function for both because they differ in two things -- whether there is
// a name field and what the button does -- and two near-identical popups would
// be two places to fix the next thing wrong with either.
void EditorApp::drawProjectPopup()
{
    const bool creating = mNewProjectOpen;
    const char* title = creating ? "New project" : "Open project";
    if ((mNewProjectOpen || mOpenProjectOpen) && !ImGui::IsPopupOpen(title))
        ImGui::OpenPopup(title);
    if (!ImGui::BeginPopupModal(title, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const auto dismiss = [this] {
        mNewProjectOpen = false;
        mOpenProjectOpen = false;
        ImGui::CloseCurrentPopup();
    };

    ImGui::TextUnformatted("Project directory");
    const float available = ImGui::GetMainViewport()->WorkSize.x;
    ImGui::SetNextItemWidth(std::clamp(available - 64.0f, 160.0f, 520.0f));
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    const bool entered =
        ImGui::InputText("##projectdir", mProjectPath, sizeof(mProjectPath),
                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (creating) {
        ImGui::TextUnformatted("Name");
        ImGui::SetNextItemWidth(std::clamp(available - 64.0f, 160.0f, 520.0f));
        ImGui::InputText("##projectname", mProjectName, sizeof(mProjectName));
    }

    const std::string dir = mProjectPath;
    std::string validation;
    if (dir.empty())
        validation = "Enter a directory.";
    else if (creating && eng::runtime::isProjectDir(dir))
        validation = "There is already a project here.";
    else if (!creating && !eng::runtime::isProjectDir(dir))
        validation = "No project.toml in this directory.";
    if (!validation.empty())
        ImGui::TextColored(kUiDanger, "%s", validation.c_str());
    else if (creating)
        ImGui::TextDisabled("Writes project.toml, a starter scene and a script.");

    ImGui::BeginDisabled(!validation.empty());
    const bool go =
        ImGui::Button(creating ? "Create" : "Open",
                      ImVec2(ui::dialogButtonWidth(), 0.0f)) ||
        (entered && validation.empty());
    if (go) {
        if (creating)
            newProject();
        else
            openProject(dir);
        dismiss();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(ui::dialogButtonWidth(), 0.0f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        dismiss();
    ImGui::EndPopup();
}

void EditorApp::drawSaveAsPopup()
{
    static constexpr const char* kTitle = "Save scene as";
    if (mSaveAsOpen && !ImGui::IsPopupOpen(kTitle)) {
        mSaveAsError.clear();
        ImGui::OpenPopup(kTitle);
    }
    if (!ImGui::BeginPopupModal(kTitle, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;
    if (!mSaveAsOpen) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    bool performPendingDiscard = false;
    ImGui::TextUnformatted("Path (.scn)");
    const float available = ImGui::GetMainViewport()->WorkSize.x;
    ImGui::SetNextItemWidth(std::clamp(available - 64.0f, 160.0f, 520.0f));
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    const bool entered =
        ImGui::InputText("##saveaspath", mSaveAsPath, sizeof(mSaveAsPath),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    std::filesystem::path candidate(mSaveAsPath);
    std::string validation;
    if (candidate.empty()) {
        validation = "Enter a scene name.";
    }
    else {
        if (!candidate.has_extension())
            candidate.replace_extension(".scn");
        if (candidate.extension() != ".scn")
            validation = "Scene files use the .scn extension.";
    }
    std::error_code existsError;
    const bool exists =
        validation.empty() && std::filesystem::exists(candidate, existsError);
    const bool replacing = exists && candidate.string() != mState.scenePath;
    if (existsError)
        validation = "The destination could not be inspected.";
    if (!mSaveAsError.empty())
        ImGui::TextColored(kUiDanger, "%s", mSaveAsError.c_str());
    else if (!validation.empty())
        ImGui::TextColored(kUiDanger, "%s", validation.c_str());
    else if (replacing)
        ImGui::TextColored(kUiWarning, "A scene already exists at this path.");

    const auto saved = [this](const std::string& path) {
        if (!saveSceneTo(path)) {
            mSaveAsError = mStatus;
            return false;
        }
        mSaveAsError.clear();
        return true;
    };
    ImGui::BeginDisabled(!validation.empty());
    const char* saveLabel = replacing ? "Replace..." : "Save";
    const bool clickedSave = ImGui::Button(saveLabel, ImVec2(ui::dialogButtonWidth(), 0.0f));
    if (clickedSave || (entered && validation.empty())) {
        const std::string path = candidate.string();
        if (replacing) {
            ConfirmDialog::open(
                "Replace the existing scene?", path,
                [this, path] {
                    if (!saveSceneTo(path)) {
                        mSaveAsError = mStatus;
                        return;
                    }
                    const bool continueDiscard = mContinueDiscardAfterSave;
                    mContinueDiscardAfterSave = false;
                    mSaveAsOpen = false;
                    if (continueDiscard)
                        performDiscard();
                },
                "Replace");
        }
        else if (saved(path)) {
            const bool continueDiscard = mContinueDiscardAfterSave;
            mContinueDiscardAfterSave = false;
            mSaveAsOpen = false;
            ImGui::CloseCurrentPopup();
            performPendingDiscard = continueDiscard;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(ui::dialogButtonWidth(), 0.0f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        mContinueDiscardAfterSave = false;
        mSaveAsOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    if (performPendingDiscard)
        performDiscard();
}

bool EditorApp::importModel(const std::string& source)
{
    const std::filesystem::path path = std::filesystem::absolute(source);

    // In process, through the engine's own Assimp importer. This used to shell
    // out to tools/editor_import_glb.py -- a second GLTF parser that read one
    // format, reported failure as "see terminal output", and could not be
    // tested without a subprocess.
    const ModelImportResult imported =
        importModelToKit(path.string(), mState.assetRoot);
    if (!imported.ok) {
        mStatus = "model import failed: " + imported.error;
        return false;
    }

    // Before the material script, always: the textures were written to disk a
    // moment ago, and Ogre indexed its resource locations at start-up. Without
    // this the material parses cleanly and every texture unit in it resolves to
    // nothing, so the model arrives in the prototype surface with no error
    // anywhere to explain it.
    mEngine->renderer().refreshAssetIndex();
    if (!imported.materialScript.empty() &&
        !mEngine->renderer().loadMaterialScript(imported.materialScript)) {
        mStatus = "model converted, but generated material did not load";
        return false;
    }

    KitCatalog catalog;
    std::string error;
    if (!KitCatalog::load(mState.kitPath, catalog, error)) {
        mStatus = "model converted, but Catalog reload failed: " + error;
        return false;
    }
    mState.catalog = std::move(catalog);
    mState.grid = GridConfig::fromCatalog(mState.catalog);

    std::vector<Command> commands;
    std::vector<AuthorId> created;
    AuthorId parent;
    const glm::vec3 position = viewFocusPoint();
    // A multi-part model gets a group to hang from, so it moves as one thing.
    if (imported.parts.size() > 1) {
        Entity group;
        group.id = mState.document.allocateId("imported_model");
        group.name = path.stem().string();
        group.transform.position = position;
        parent = group.id;
        created.push_back(group.id);
        commands.push_back(makeCreateEntity(std::move(group)));
    }

    for (const ImportedPart& part : imported.parts) {
        if (!mState.catalog.find(part.prefab)) {
            mStatus = "imported prefab is missing from reloaded Catalog: " +
                      part.prefab;
            return false;
        }
        Entity entity;
        entity.id = mState.document.allocateId(part.prefab);
        entity.name = part.name.empty() ? entity.id : part.name;
        entity.prefab = part.prefab;
        entity.parent = parent;
        entity.castShadows = true;
        entity.transform.position =
            parent.empty() ? position + part.offset : part.offset;
        created.push_back(entity.id);
        commands.push_back(makeCreateEntity(std::move(entity)));
    }

    runCommand(
        makeComposite("import " + path.stem().string(), std::move(commands)));
    mState.selection = created;
    mSelectionAnchor = created.back();
    mOutlinerReveal = created.back();
    mPreview->invalidate();
    mMaterialNames = mEngine->renderer().materialNames();
    std::sort(mMaterialNames.begin(), mMaterialNames.end());
    mMaterialCatalog.clear();
    mMaterialCatalogLoaded = false;

    mStatus = "imported " + path.filename().string() + " as " +
              std::to_string(imported.parts.size()) + " mesh part(s)";
    if (!imported.textures.empty())
        mStatus +=
            ", " + std::to_string(imported.textures.size()) + " texture(s)";
    // Warnings are the difference between "it looks wrong" and "it looks wrong
    // BECAUSE the texture was not beside the model".
    mImportWarnings = imported.warnings;
    if (!mImportWarnings.empty())
        mStatus += " (" + std::to_string(mImportWarnings.size()) +
                   " warning(s) -- see Status)";
    return true;
}

void EditorApp::drawImportModelPopup()
{
    if (mImportModelOpen) {
        ImGui::OpenPopup("Import model");
        mImportModelOpen = false;
        mImportEntriesKey.clear();
    }
    const ImVec2 work = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(
        ImVec2(std::min(620.0f, std::max(280.0f, work.x - 32.0f)), 0.0f),
        ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Import model", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextWrapped("Import a supported static model. Mesh node transforms, "
                       "UVs, vertex colours and base-colour textures are baked "
                       "into engine OBJ parts. Skins and animation are not "
                       "imported.");
    ImGui::Spacing();

    // Every format this engine's Assimp build can read, asked of the engine
    // rather than listed here: a hardcoded list is one that silently disagrees
    // with the loader the moment Assimp is rebuilt with different options.
    static const std::vector<std::string> kModelExtensions =
        eng::Renderer::supportedModelExtensions();

    // Two ways in, because source art arrives two ways. The scan finds what is
    // already filed in the project however deep it sits; browsing reaches a
    // download that has not been filed yet.
    if (ImGui::RadioButton("search the project", !mImportBrowsing))
        mImportBrowsing = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("browse folders", mImportBrowsing))
        mImportBrowsing = true;

    std::vector<FileEntry> entries;
    if (mImportBrowsing) {
        if (mImportDir.empty())
            mImportDir = mImportScanRoot;
        ImGui::TextDisabled("%s", mImportDir.c_str());
        const std::string parent = parentDirectory(mImportDir);
        ImGui::BeginDisabled(parent.empty());
        if (ImGui::SmallButton("up"))
            mImportDir = parent;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("project"))
            mImportDir = mImportScanRoot;
        ImGui::SameLine();
        if (ImGui::SmallButton("home"))
            if (const char* home = std::getenv("HOME"))
                mImportDir = home;
    }
    else {
        ImGui::TextDisabled("%s", mImportScanRoot.c_str());
    }

    // Recursive filesystem scans were running every frame the modal was open,
    // making typing and row hover hitch on larger source trees. Cache by mode
    // and directory; Refresh is explicit and cheap to discover.
    const std::string scanKey =
        std::string(mImportBrowsing ? "browse:" : "project:") +
        (mImportBrowsing ? mImportDir : mImportScanRoot);
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh"))
        mImportEntriesKey.clear();
    if (mImportEntriesKey != scanKey) {
        mImportEntriesKey = scanKey;
        if (mImportBrowsing) {
            mImportEntries = listDirectory(mImportDir, kModelExtensions);
            mImportEntriesTruncated = false;
        }
        else {
            const ScanResult scan =
                findFiles(mImportScanRoot, kModelExtensions);
            mImportEntries = scan.files;
            mImportEntriesTruncated = scan.truncated;
        }
    }
    entries = mImportEntries;

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##modelfilter", "filter", mImportFilter,
                             sizeof(mImportFilter));
    entries = filterFiles(entries, mImportFilter);

    if (ImGui::BeginChild("##models", ImVec2(0.0f, 260.0f),
                          ImGuiChildFlags_Borders)) {
        if (entries.empty()) {
            ImGui::TextDisabled(mImportFilter[0]
                                    ? "nothing matches"
                                    : "no supported model files here");
        }
        for (const FileEntry& entry : entries) {
            ImGui::PushID(entry.path.c_str());
            if (entry.directory) {
                if (ImGui::Selectable((entry.label + "/").c_str(), false,
                                      ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    mImportDir = entry.path;
                    mImportFilter[0] = '\0';
                }
            }
            else {
                const bool chosen = mImportModelPath == entry.path;
                if (ImGui::Selectable(entry.label.c_str(), chosen,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    mImportModelPath = entry.path;
                    // Double-click imports: the gesture everyone already tries.
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                        importModel(mImportModelPath))
                        ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", entry.path.c_str());
            }
            ImGui::PopID();
        }
        if (mImportEntriesTruncated)
            ImGui::TextDisabled(
                "(list truncated -- filter, or browse folders)");
    }
    ImGui::EndChild();

    ImGui::TextDisabled("%s",
                        mImportModelPath.empty()
                            ? "select a model, or double-click to import it"
                            : mImportModelPath.c_str());

    ImGui::BeginDisabled(mImportModelPath.empty());
    if (ImGui::Button("Import", ImVec2(ui::dialogButtonWidth(), 0.0f))) {
        if (importModel(mImportModelPath))
            ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(ui::dialogButtonWidth(), 0.0f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// The bottom bar: the panel buttons on the left, what the editor is doing on
// the right. Godot's, and one row cheaper than a button strip plus a status
// line -- which is what this was, before the bottom rail left the dockspace.
void EditorApp::drawStatusBar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(8.0f * mAppliedUiScale, 4.0f * mAppliedUiScale));
    if (ImGui::BeginChild("##workspace_status", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse)) {
        // --- left: the bottom panel's tabs -----------------------------------
        for (int i = 0; i < kBottomTabCount; ++i) {
            const BottomTab tab = BottomTab(i);
            const bool open = mBottom.isOpen(tab);
            if (i > 0)
                ImGui::SameLine();
            if (open)
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImGui::GetStyleColorVec4(
                                          ImGuiCol_Header));
            // Problems carries its count, which is the reason to look at it.
            // Zero is drawn as nothing rather than as "0": a badge that is
            // always there stops being a signal.
            std::string label = bottomTabName(tab);
            int errors = 0;
            if (tab == BottomTab::Problems) {
                for (const Issue& issue : mIssues)
                    errors += issue.severity == Severity::Error ? 1 : 0;
                if (errors > 0)
                    label += "  " + std::to_string(errors);
            }
            if (errors > 0)
                ImGui::PushStyleColor(ImGuiCol_Text, kUiDanger);
            if (ImGui::Button(label.c_str()))
                mBottom.toggle(tab);
            if (errors > 0)
                ImGui::PopStyleColor();
            if (open)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s%s", bottomTabName(tab),
                                  open ? " -- click to close" : "");
            }
        }

        // --- right: what the editor is doing ---------------------------------
        //
        // Measured and right-aligned rather than tabled: the left half is a
        // variable number of buttons, and a fixed column layout across both
        // would leave a gap that grows with the theme's padding.
        const ContractReport& report = contract();
        const std::string scene =
            mState.scenePath.empty()
                ? std::string("untitled")
                : std::filesystem::path(mState.scenePath).filename().string();

        std::string right;
        if (!mPreview->lastError().empty())
            right = "preview: " + mPreview->lastError();
        else if (!mImportWarnings.empty())
            right = std::to_string(mImportWarnings.size()) + " import warning" +
                    (mImportWarnings.size() == 1 ? "" : "s");
        else
            right = mStatus.empty() ? "ready" : mStatus;

        std::string context = scene + (mState.dirty ? " *" : "");
        context += "   |   ";
        context += sceneKindName(report.kind);
        if (!mState.selection.empty()) {
            const Entity* primary = mState.document.find(*mState.primary());
            const std::string name =
                primary ? (primary->name.empty() ? primary->id : primary->name)
                        : std::string("(gone)");
            context += "   |   " + name;
            if (mState.selection.size() > 1)
                context += " +" + std::to_string(mState.selection.size() - 1);
        }
        else {
            const glm::vec3 eye = mState.camera.activeEye();
            char at[64];
            std::snprintf(at, sizeof(at), "   |   %.1f %.1f %.1f",
                          double(eye.x), double(eye.y), double(eye.z));
            context += at;
        }

        const float contextWidth = ImGui::CalcTextSize(context.c_str()).x;
        const float available = ImGui::GetContentRegionMax().x;
        const float contextX = available - contextWidth;
        // The activity line takes whatever is between the buttons and the
        // context, and is clipped rather than allowed to push it off the edge:
        // where the scene is and whether it is saved must never be the part
        // that falls off.
        ImGui::SameLine();
        const float statusX = ImGui::GetCursorPosX() + 16.0f;
        if (contextX > statusX + 40.0f) {
            ImGui::SetCursorPosX(statusX);
            ImGui::PushTextWrapPos(contextX - 12.0f);
            if (!mPreview->lastError().empty())
                ImGui::TextColored(kUiDanger, "%s", right.c_str());
            else if (!mImportWarnings.empty())
                ImGui::TextColored(kUiWarning, "%s", right.c_str());
            else
                ImGui::TextUnformatted(right.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", right.c_str());
            if (!mImportWarnings.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("dismiss"))
                    mImportWarnings.clear();
            }
            ImGui::SameLine();
        }
        if (contextX > statusX)
            ImGui::SetCursorPosX(contextX);
        ImGui::TextDisabled("%s", context.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n%s\ncook: %s\nundo: %s",
                              mState.scenePath.empty() ? "not saved"
                                                       : mState.scenePath.c_str(),
                              sceneKindSummary(report.kind),
                              mCookStatus.c_str(),
                              mCommands.canUndo() ? mCommands.undoLabel().c_str()
                                                  : "empty");
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void EditorApp::onShutdown(eng::Engine& engine)
{
    for (eng::ParticlesHandle h : mParticlePreviews)
        engine.renderer().despawnParticles(h);
    mParticlePreviews.clear();
    if (mParticleThumbnail.valid())
        engine.renderer().despawnParticles(mParticleThumbnail);
    if (mParticleThumbnailNode.valid())
        engine.renderer().destroyNode(mParticleThumbnailNode);
    mPreview.reset();
    stopAudioPreview();
    if (mAudio)
        mAudio->terminate();
    mAudio.reset();
}

} // namespace ed
