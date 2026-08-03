#pragma once
#include <editor/scene/BrushPlacement.h>
#include <editor/commands/Clipboard.h>
#include <editor/commands/CommandPalette.h>
#include <editor/commands/Commands.h>
#include <editor/ui/ConfirmDialog.h>
#include <editor/scene/DocumentRaycast.h>
#include <editor/ui/EditorIcons.h>
#include <editor/assets/FileBrowser.h>
#include <editor/viewport/EntityGizmos.h>
#include <editor/assets/GameVocabulary.h>
#include <editor/assets/MaterialCatalog.h>
#include <editor/assets/ModelImportPipeline.h>
#include "RenderPalette.h"
#include <editor/viewport/ViewportOverlay.h>
#include <editor/project/SceneBrowser.h>
#include <editor/content/SceneValidate.h>
#include <editor/project/EditorSettings.h>
#include <editor/app/EditorState.h>
#include <editor/scene/EntityComponents.h>
#include <editor/ui/OutlinerPanel.h>
#include <editor/ui/UiStage.h>
#include <editor/ui/MaterialStage.h>
#include <editor/scene/OutlinerTree.h>
#include <editor/scene/Picker.h>
#include <editor/viewport/PreviewBridge.h>
#include <editor/content/SceneTemplates.h>
#include <editor/project/RunGame.h>

#include <eng/app/Application.h>
#include <eng/debug/Console.h>
#include <eng/particles/ParticleLibrary.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace ed {

// The placement editor: a second consumer of the engine, beside the game.
//
// It is an eng::Application rather than an eng::FpsGameApp because almost
// nothing the genre base provides applies here -- no physics step, no player
// controller, no mouse capture. What it does share is the frame ordering, which
// is the whole reason that abstraction exists.
//
// The window is ImGui end to end: the world appears inside it as an
// ImGui::Image of the renderer's offscreen viewport. That inversion is what
// makes this editor viable at all -- ImGui drawn *over* the Ogre overlay is
// what made the previous one flicker badly enough to be deleted.
class EditorApp : public eng::Application {
public:
    EditorApp();
    ~EditorApp() override;

    eng::AppConfig configure(int argc, char** argv) override;
    void onLoad(eng::Engine& engine, eng::LoadPlan& plan) override;
    bool onStart(eng::Engine& engine) override;
    void onFrameBegin(const eng::FrameContext& f) override;
    void onUpdate(const eng::FrameContext& f) override;
    void onGui(const eng::FrameContext& f) override;
    void onShutdown(eng::Engine& engine) override;

private:
    void drawMenuBar(const eng::FrameContext& f);
    void drawToolbar();
    void drawViewport(const eng::FrameContext& f);
    // The 2D viewport: the game's real HUD drawn against a dialled-in state.
    void drawUiStage();
    void drawOutliner();
    // Select from outside the outliner (viewport pick, placement, a jump from
    // the validation list) and ask the panel to scroll to the row.
    void selectAndReveal(const game::content::AuthorId& id, bool toggle);
    // A viewport click hits a mesh; this says which entity that click means --
    // the composed object it belongs to, or the exact piece when the author is
    // already working inside it (or holding Alt).
    game::content::AuthorId
    resolvePick(const game::content::AuthorId& hit) const;
    void drawInspector();
    // The grouped tree the outliner draws, rebuilt only when the document or
    // the panel's own options change: it walks and sorts every entity, and the
    // panel is open while the gizmo is being dragged.
    const OutlinerTree& outlinerTree();
    void selectGroup(const OutlinerGroup& group, bool add);
    // Hangs `child` under `parent` (empty detaches it), keeping the place it is
    // drawn in. Refuses anything that would close a loop.
    void reparentEntity(const game::content::AuthorId& child,
                        const game::content::AuthorId& parent);
    // The same edit, built but not run, so a batch closes as one undo entry.
    // False when the move is refused or would change nothing.
    bool buildReparent(const game::content::AuthorId& child,
                       const game::content::AuthorId& parent, Command& out);
    // Multi-selection shortcuts for the same thing: compose, and break apart.
    void parentSelectionToPrimary();
    void detachSelection();
    // Hides everything the selection does not need. Session state, not a
    // document edit -- the show-everything button is its undo.
    void isolateSelection();
    void focusPanelIfRequested(const char* name);
    void drawSelectionContextMenu();
    // Component editing, applied to the whole selection as one undo entry --
    // "give these forty pillars a collider" is the reason the panel groups.
    void addComponentToSelection(const ComponentType& type);
    void removeComponentFromSelection(const ComponentType& type);
    ComponentDefaults componentDefaults() const;
    // The level's own properties, shown where the entity inspector would be
    // when nothing is selected.
    void drawSceneProperties();
    void setScenePalette(const std::string& palette);
    void drawStatusBar();
    void drawCatalog();
    void drawIssues();
    void drawMaterialPanel();
    void drawParticlePanel();
    void applyMaterialToSelection(const std::string& material);
    // The shipped .material scripts, classified by what geometry they need.
    // Loaded once, on first use.
    const std::vector<MaterialInfo>& materialCatalog();
    const MaterialInfo* materialInfo(const std::string& name);
    // What the selected entities' meshes offer a material, taken from the
    // material their kit pieces were authored with.
    MeshKind selectionMeshKind();
    void setMode(bool material);
    // Selection and manipulation, both driven from inside the viewport panel so
    // they share its rect with ImGuizmo.
    void handleViewportPicking(const eng::FrameContext& f);
    void drawGizmo(const eng::FrameContext& f);
    void finishGizmoDrag();
    void drawStageGizmo(const eng::FrameContext& f);
    void runCommand(Command command);
    void finishInspectorEdit();
    // Undo/redo as one call. The menu, the keybind and the palette all reach
    // for it, and the three copies this replaced had already drifted: one of
    // them forgot to invalidate the preview, so an undone placement stayed on
    // screen until the next unrelated edit.
    void applyHistory(bool redo);
    // Every verb the editor has, named. Rebuilt each time the palette opens,
    // because half of them are only available -- or only meaningful -- given
    // the current selection, tool and document.
    std::vector<PaletteAction> paletteActions();
    bool saveScene();
    bool saveSceneTo(const std::string& path);
    void newScene(game::content::SceneTemplate which);
    void drawSaveAsPopup();
    void drawImportModelPopup();
    // Converts a source model into kit pieces and drops them into the scene.
    // Any format this build of Assimp reads; the conversion itself lives in
    // ModelImportPipeline, which has no editor state and can be tested.
    bool importModel(const std::string& path);
    // Opening an existing scene: the dialog, and the one path everything else
    // (recent list, palette, console) goes through so the unsaved-work prompt
    // can never be skipped.
    void drawOpenScenePopup();
    void requestOpen(const std::string& path);
    // Clipboard. Cut is copy plus delete, in that order, as one undo entry.
    void copySelection(bool cut);
    void pasteClipboard();
    // Adds already-built copies as one undo entry and selects them. Duplicate
    // and paste differ only in where the entities came from.
    void addCopies(const std::vector<game::content::Entity>& copies,
                   const std::string& label);
    // Periodic backup of the open document, and the recovery it enables.
    void tickAutosave(float dt);
    bool writeAutosave();
    void recoverAutosave();
    // Preferences, and the one panel that edits them. Written on every change
    // rather than on quit: an editor that loses its settings when it crashes is
    // an editor whose settings nobody trusts.
    void drawSettings();
    void commitSettings();
    // The reverse: fold the live view toggles back into the settings and save
    // them if any moved. Once a frame, because the menus edit the state.
    void captureSettings();
    // Push the loaded preferences onto the things they configure: the render
    // profile, the lighting mode and the view toggles. Called once at start-up
    // and again whenever the panel changes one, so there is one direction of
    // travel -- settings to state, never back.
    void applySettings();
    void applyUiScale(float scale);
    void handleShortcuts(const eng::FrameContext& f);
    // PSX_EDITOR_SELFTEST: run the edits the UI performs, one per frame, then
    // quit. The four ways the editor was reported to crash -- deleting an
    // entity, removing a component, removing a mesh, unparenting -- are all
    // mouse gestures, which is why they were only ever reproduced by hand.
    // Driving them through the same functions the widgets call makes them a
    // test, and a stack trace.
    void runSelfTest(eng::Engine& engine);
    int mSelfTestStep = -1; // <0 = off
    // The name of the render profile a playtest should launch with, resolved
    // from the settings and the viewport's current one. Empty means "let the
    // game pick", which is what the engine default is.
    std::string playtestPresetName() const;
    // Every keybind, in one table, drawn as a panel. The bindings themselves
    // live in editor.toml; this is the reading of them.
    void drawHelp();
    // Screen-space marks for the entities with no mesh -- spawns, lights,
    // triggers, markers. Rebuilt on a document change, not per frame.
    const std::vector<GizmoMark>& entityGizmoMarks();
    void drawEntityGizmos();
    // The strip of view toggles inside the 3D view, and the cost readout in its
    // corner. Both belong to the viewport rather than to the toolbar panel:
    // they answer questions about what is on screen.
    void drawViewportToolbar();
    void drawViewportStats(const eng::FrameContext& f);

    // Anything that throws the open document away. An hour of blockout is the
    // most expensive thing in this program and four separate paths used to
    // discard it without asking -- Escape most dangerously of all, since that
    // is the key people press to leave a mode, not the editor.
    enum class Discard { Quit, Reload, NewScene, Open };
    // Runs `what` immediately on a clean document, otherwise parks it behind
    // the save/discard/cancel prompt.
    void requestDiscard(Discard what, game::content::SceneTemplate which =
                                          game::content::SceneTemplate::Empty);
    void performDiscard();
    void drawDiscardPopup();
    // F6 / F5: cook the authored scene to a runtime map, and play it.
    bool cookScene(std::string& mapPath);
    void runPlaytest();
    void deleteSelection();
    // The delete itself, past the confirmation. Separate because a confirmed
    // delete runs a frame later, from the dialog's callback, by which time the
    // selection may have moved on -- the ids are what it acts on.
    void commitDelete(const std::vector<game::content::AuthorId>& ids);
    // Above this many entities, a delete asks first. Small enough that a room
    // drag counts, large enough that nudging a handful never does.
    static constexpr std::size_t kConfirmDeleteAbove = 8;
    void duplicateSelection();
    Ray mouseRay() const;
    // What the cursor is over, honouring hidden, locked and -- while a stroke
    // is running -- the pieces that stroke has already laid down. Placement
    // reads it for height; erase and the eyedropper read it for a target.
    DocumentHit hoveredEntity(bool ignoreStroke = false) const;
    // Placement: the ghost under the cursor, and committing it.
    Placement hoveredPlacement() const;
    void placeAt(const Placement& placement);
    void drawPlacementGhost();
    // Shift+drag: the inverse stroke. Shares the whole shape of painting --
    // one target per slot, applied live, closed as a single undo entry.
    void eraseAt(const DocumentHit& hit);
    void finishStroke();
    // Alt+click: adopt what the cursor is over as the brush, so a room can be
    // extended without hunting its pieces down in the Catalog.
    void pickBrushFrom(const DocumentHit& hit);
    // Room tool: drag a rectangle of cells, get a finished room.
    bool hoveredCell(int& col, int& row) const;
    void drawRoomPreview(const eng::FrameContext& f);
    void commitRoom();
    // Portal is the one visible compound: its mesh, shader parameters and exit
    // meaning belong to one entity.
    //
    // Built but not added, so the brush and the one remaining button produce
    // identical entities: the component registry's defaults, one code path.
    game::content::Entity
    makeGameplayEntity(Gameplay kind,
                       const game::content::XformAuthor& transform) const;
    void addGameplayEntity(Gameplay kind);
    glm::vec3 viewFocusPoint() const;
    void updateGridLines(eng::Renderer& renderer);
    void applySceneEnvironment(eng::Renderer& renderer);
    bool loadScene(const std::string& path);
    // World-space bounds of a set of entities (all of them when `ids` is
    // empty), from the catalogue's own sizes -- the preview's meshes may not be
    // loaded yet, and the editor must be able to frame a scene regardless.
    bool boundsOf(const std::vector<game::content::AuthorId>& ids,
                  glm::vec3& min, glm::vec3& max) const;
    void frameCamera(const glm::vec3& min, const glm::vec3& max);
    void frameSelectionOrAll();
    // Drop to the player's eye at the spawn, or come back to where the author
    // was. Judging whether a room reads -- is the exit legible, is the ceiling
    // oppressive -- only works from head height, and the alternative was F5.
    void toggleWalk();

    // Shared engine developer console, docked with Status/Issues. Registers the
    // editor's own commands in onStart; everything else about it is engine
    // code, so the editor never grows a second log window.
    void installConsoleCommands();

    EditorState mState;
    eng::DebugConsole mConsole;
    std::unique_ptr<PreviewBridge> mPreview;
    // Held from onStart. The mode switch and the material panel need the
    // renderer outside a frame callback, and the Engine outlives this app.
    eng::Engine* mEngine = nullptr;
    std::string mPendingScene; // from the command line, opened in onStart
    // Set by the load step: the catalog is what everything else is placed
    // against, so a failure there has to abort the run at onStart.
    bool mCatalogFailed = false;
    std::string mStatus; // one-line feedback under the panels

    // Viewport geometry, in window pixels: where the offscreen image is drawn.
    // ImGuizmo will need exactly this rect, so it is kept as state rather than
    // recomputed.
    float mViewportX = 0.0f, mViewportY = 0.0f;
    float mViewportW = 0.0f, mViewportH = 0.0f;
    // RTT dimensions are physical pixels; interaction remains in ImGui's
    // logical coordinates. Tracking both makes DPI-only changes resize the RTT.
    int mViewportTextureW = 0, mViewportTextureH = 0;
    bool mViewportHovered = false;
    bool mFlying = false;
    bool mLayoutBuilt = false;
    // How many level cells one drawn grid cell covers right now: 1 up close, a
    // power of two once the camera pulls back. Shown in the toolbar so a coarse
    // grid never misreports the scale.
    int mGridMultiple = 1;
    std::size_t mBatches = 0, mTriangles = 0;

    CommandStack mCommands;
    EntityEditTransaction mInspectorEdit;
    // Gizmo drag state: the transform as it was when the drag began, captured
    // once so the whole drag closes as a single undo entry.
    bool mGizmoDragging = false;
    bool mGizmoHovered = false;
    // Frozen for the duration of a drag. The live anchor is the centre of the
    // selection's bounds, which moves as the selection rotates -- feeding that
    // back into the gizmo each frame makes the pivot crawl mid-drag.
    glm::vec3 mDragAnchor{0.0f};
    // ImGuizmo mutates this cumulatively for the full pointer drag. Rebuilding
    // from the already-edited document each frame compounds translation and
    // loses rotation deltas when the operation changes.
    glm::mat4 mDragGizmoMatrix{1.0f};
    std::vector<std::pair<game::content::AuthorId, game::content::XformAuthor>>
        mDragStart;
    int mGizmoOperation = 0;  // -1 pick only, 0 move, 1 rotate, 2 scale
    bool mGizmoLocal = false; // world by default; the grid is the usual frame
    // Off by default: placing things needs flat bright light. On, the viewport
    // uses the scene's own lighting, which is the only way to see whether the
    // level actually guides the eye.
    bool mGameLighting = false;

    // Place tool: a stroke drops one piece per cell/edge the cursor crosses (or
    // removes one per entity it crosses, held with Shift), and the whole drag
    // closes as one undo entry.
    //
    // A set rather than a vector: the scan is per placement per frame, so a
    // drag across a large room was quadratic in the pieces it had already laid.
    enum class Stroke { None, Paint, Erase };
    Stroke mStroke = Stroke::None;
    std::unordered_set<std::string> mStrokeSlots;
    std::vector<Command> mStrokeParts;
    std::vector<game::content::AuthorId> mStrokeIds;
    // Room drag: the cell the drag started on, and whether one is in progress.
    bool mRoomDragging = false;
    int mRoomStartCol = 0, mRoomStartRow = 0;
    char mCatalogFilter[64] = {};

    // Issues, recomputed when the document changes rather than every frame.
    std::vector<game::content::Issue> mIssues;
    uint64_t mIssuesRevision = ~uint64_t(0);
    std::string mCookStatus = "not cooked";
    RunHandle mPlaytest;
    // F5 starts the playtest at the editor camera rather than at the scene's
    // spawn. On by default: while building, "does this room work" is the
    // question, and it is asked about the room on screen. Turn it off to check
    // the arrival itself.
    bool mPlayFromCamera = true;
    std::string mExecutablePath; // argv[0], for finding the game binary

    // Material staging mode. A separate scene rather than a panel over the
    // level: the point of a reference stage is that nothing else is in it.
    MaterialStage mStage;
    bool mMaterialMode = false;
    bool mStageAutoSpin = true;
    float mStageSpinSpeed = 0.35f;
    std::vector<std::string> mMaterialNames;
    std::vector<MaterialInfo> mMaterialCatalog;
    bool mMaterialCatalogLoaded = false;
    // Off by default: the catalogue is mostly materials that cannot go on an
    // entity, and offering them is how a compositor pass ends up on a wall.
    bool mShowAllMaterials = false;
    // Frames of pending focus for the Material tab. A count rather than a
    // flag: the docked layout re-selects its saved tab a frame after the
    // request lands, so one request is not enough.
    int mFocusMaterialFrames = 0;
    // A docked panel to bring forward for the first few frames, named by
    // PSX_EDITOR_PANEL. Verification only: panels share tabs, and a screenshot
    // run has no mouse to click one with.
    std::string mFocusPanel;
    int mFocusPanelFrames = 6;
    // The vocabularies the game defines, read once at startup. Empty is
    // survivable: the inspector falls back to free text and says so.
    std::vector<std::string> mEnemyIds;
    std::vector<std::string> mPickupIds;
    // The looks palettes.toml defines, and where it is. Resolved once: the
    // combo is drawn every frame the inspector is open with nothing selected.
    std::vector<std::string> mPalettes;
    std::string mPalettesPath;
    char mMaterialFilter[64] = {};

    // Particle authoring. The library owns the descs; the panel edits them in
    // place and re-registers, so a change is visible in the viewport on the
    // next frame without a restart.
    eng::ParticleLibrary mParticles;
    int mParticleSelected = -1;
    char mParticleFilter[64] = {};
    std::vector<eng::ParticlesHandle> mParticlePreviews;
    float mParticlePreviewScale = 1.0f;
    // The offscreen swatch has one subject at a time: a lit sphere wearing a
    // material, or a running particle effect. These route every request through
    // one place so the Inspector, the Material panel and the Particles panel
    // hand it over cleanly instead of half-configuring it behind each other.
    // Both deduplicate, so calling them every frame for a hovered row is free.
    void requestMaterialPreview(const std::string& material);
    void requestEffectPreview(const std::string& effect);
    enum class PreviewSubject { None, Material, Effect };
    PreviewSubject mPreviewSubject = PreviewSubject::None;
    std::string mPreviewName;

    eng::NodeHandle mParticleThumbnailNode;
    eng::ParticlesHandle mParticleThumbnail;
    std::string mParticleThumbnailEffect;
    float mParticleThumbnailScale = 1.0f;
    double mParticleThumbnailRestartAt = 0.0;
    bool mParticlesDirty = false;
    int mFloorVariant = 0; // 0 default grey, 1 dark
    std::string mSelectedMaterial;
    bool mSaveAsOpen = false;
    bool mContinueDiscardAfterSave = false;
    char mSaveAsPath[512] = {};
    std::string mSaveAsError;
    bool mImportModelOpen = false;
    // The model picker. The path is chosen from a listing rather than typed:
    // the one GLB in this repository is four directories deep with spaces in
    // two of them, which made the old text field a spelling test.
    std::string mImportModelPath;
    std::string mImportScanRoot; // assets/source, resolved in onStart
    std::string mImportDir;      // where "browse folders" currently is
    bool mImportBrowsing = false;
    char mImportFilter[64] = {};
    std::vector<FileEntry> mImportEntries;
    std::string mImportEntriesKey;
    bool mImportEntriesTruncated = false;
    // What the last import could not do -- a texture that was not beside the
    // model, an embedded one it declined to guess at. Shown in the Status
    // panel, because "the prop is untextured" needs its reason next to it.
    std::vector<std::string> mImportWarnings;
    // Set when a discard is waiting on the prompt; consumed by performDiscard.
    bool mDiscardOpen = false;
    Discard mDiscardWhat = Discard::Quit;
    game::content::SceneTemplate mDiscardTemplate =
        game::content::SceneTemplate::Empty;
    PaletteState mPalette;
    RecentScenes mRecent;
    std::string mRecentFile; // artifacts/editor/recent.txt, resolved in onLoad
    bool mOpenSceneOpen = false;
    char mOpenFilter[64] = {};
    std::vector<SceneEntry> mOpenSceneEntries;
    std::string mOpenSceneSelection;
    std::string mPendingOpen; // parked behind the unsaved-work prompt
    // Entities lifted with Ctrl+C, held as authored data rather than as ids:
    // the originals may be deleted, or the document replaced, before the paste.
    std::vector<game::content::Entity> mClipboard;
    // Preferences and where they live. Loaded next to recent.txt, for the same
    // reason: both are per-user state about the editor, not content.
    EditorSettings mSettings;
    std::string mSettingsFile; // artifacts/editor/settings.txt
    bool mSettingsOpen = false;
    float mAppliedUiScale = 1.0f;
    // Seconds until the next backup. The interval is a setting now
    // (EditorSettings::autosaveSeconds); losing two minutes of blockout is an
    // annoyance, losing an hour is the afternoon.
    float mAutosaveIn = EditorSettings{}.autosaveSeconds;
    bool mAutosaveOffered = false; // recovery is proposed once per scene
    bool mHelpOpen = false;
    std::vector<GizmoMark> mGizmoMarks;
    uint64_t mGizmoMarksRevision = ~uint64_t(0);
    // Both on by default: an editor that hides half the level until a checkbox
    // is found is the state this feature was written to end.
    bool mShowEntityGizmos = true;
    bool mShowGizmoVolumes = true;
    bool mShowFrameStats = true;
    bool mShowGrid = true;
    FrameStatsSmoother mFrameStats;
    FrameBudget mFrameBudget;
    char mOutlinerFilter[64] = {};
    bool mOutlinerShowGeometry = true;
    OutlinerTree mOutliner;
    uint64_t mOutlinerRevision = ~uint64_t(0);
    OutlinerOptions mOutlinerOptions;
    // The rows as the panel last drew them, which is what a Shift-click
    // resolves a range against (see OutlinerRowOrder).
    OutlinerRowOrder mOutlinerRows;
    // The 2D viewport's state and the HUD it drives. The HUD is the game's own
    // class, constructed once here, so what the panel shows is what ships.
    UiStageState mUiStage;
    game::GameHud mUiHud;
    bool mUiHudReady = false;
    // Where the next Shift-range starts: the last row clicked without Shift.
    // Cleared when the document changes under it, so a range can never reach
    // back to a row that no longer exists.
    game::content::AuthorId mSelectionAnchor;
    // A row the panel should scroll to and open its way down to, consumed on
    // the frame it is honoured. Set when the selection came from anywhere but
    // the panel -- a viewport pick, a command, an undo.
    game::content::AuthorId mOutlinerReveal;
    // Whose material the swatch is currently showing, so the preview follows a
    // change of selection without overriding a click in the material list.
    game::content::AuthorId mPreviewedEntity;
    // Effect names for the Particles component's combo, refreshed per frame.
    std::vector<std::string> mParticleEffectNames;
    char mAddComponentFilter[64] = {};
    bool mThumbAutoSpin = true;
    bool mCycleMaterials = false;
    std::size_t mCycleIndex = 0;
    EditorCamera mCameraBeforeMode;
};

} // namespace ed
