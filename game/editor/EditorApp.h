#pragma once
#include "Commands.h"
#include "SceneValidate.h"
#include "EditorState.h"
#include "MaterialStage.h"
#include "Picker.h"
#include "PreviewBridge.h"
#include "SceneTemplates.h"
#include "RunGame.h"

#include <eng/app/Application.h>
#include <eng/debug/Console.h>
#include <eng/particles/ParticleLibrary.h>

#include <memory>
#include <string>

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
class EditorApp : public eng::Application
{
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
    void drawOutliner();
    void drawInspector();
    void drawStatusBar();
    void drawCatalog();
    void drawIssues();
    void drawMaterialPanel();
    void drawParticlePanel();
    void applyMaterialToSelection(const std::string& material);
    void setMode(bool material);
    // Selection and manipulation, both driven from inside the viewport panel so
    // they share its rect with ImGuizmo.
    void handleViewportPicking(const eng::FrameContext& f);
    void drawGizmo(const eng::FrameContext& f);
    void drawStageGizmo(const eng::FrameContext& f);
    void runCommand(Command command);
    bool saveScene();
    void newScene(game::content::SceneTemplate which);
    void drawSaveAsPopup();

    // Anything that throws the open document away. An hour of blockout is the
    // most expensive thing in this program and four separate paths used to
    // discard it without asking -- Escape most dangerously of all, since that
    // is the key people press to leave a mode, not the editor.
    enum class Discard { Quit, Reload, NewScene };
    // Runs `what` immediately on a clean document, otherwise parks it behind
    // the save/discard/cancel prompt.
    void requestDiscard(Discard what,
                        game::content::SceneTemplate which =
                            game::content::SceneTemplate::Empty);
    void performDiscard();
    void drawDiscardPopup();
    // F6 / F5: cook the authored scene to a runtime map, and play it.
    bool cookScene(std::string& mapPath);
    void runPlaytest();
    void deleteSelection();
    void duplicateSelection();
    Ray mouseRay() const;
    // Placement: the ghost under the cursor, and committing it.
    bool hoveredPlacement(game::content::CellPlacement& cell,
                           game::content::XformAuthor& transform) const;
    void placeAt(const game::content::CellPlacement& cell,
                 const game::content::XformAuthor& transform);
    void drawPlacementGhost();
    // Room tool: drag a rectangle of cells, get a finished room.
    bool hoveredCell(int& col, int& row) const;
    void drawRoomPreview(const eng::FrameContext& f);
    void commitRoom();
    // Non-kit entities: markers, spawns, encounters, lights, volumes. They have
    // no prefab and no mesh, so they are created straight in front of the
    // camera rather than placed with the kit brush.
    enum class Gameplay {
        PlayerSpawn, Exit, Marker, EnemySpawn, Pickup, Trigger, PointLight,
        DirectionalLight,
    };
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
    std::string mStatus;       // one-line feedback under the panels

    // Viewport geometry, in window pixels: where the offscreen image is drawn.
    // ImGuizmo will need exactly this rect, so it is kept as state rather than
    // recomputed.
    float mViewportX = 0.0f, mViewportY = 0.0f;
    float mViewportW = 0.0f, mViewportH = 0.0f;
    bool mViewportHovered = false;
    bool mFlying = false;
    bool mLayoutBuilt = false;
    std::size_t mBatches = 0, mTriangles = 0;

    CommandStack mCommands;
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
    int mGizmoOperation = 0; // 0 translate, 1 rotate, 2 scale
    bool mGizmoLocal = false; // world by default; the grid is the usual frame
    // Off by default: placing things needs flat bright light. On, the viewport
    // uses the scene's own lighting, which is the only way to see whether the
    // level actually guides the eye.
    bool mGameLighting = false;

    // Place tool: painting drops one piece per cell/edge the cursor crosses,
    // and the whole drag closes as one undo entry.
    bool mPainting = false;
    std::vector<std::string> mPaintedSlots;
    std::vector<Command> mPaintParts;
    std::vector<game::content::AuthorId> mPaintedIds;
    int mBrushYawQuarters = 0;
    // Room drag: the cell the drag started on, and whether one is in progress.
    bool mRoomDragging = false;
    int mRoomStartCol = 0, mRoomStartRow = 0;
    char mCatalogFilter[64] = {};

    // Issues, recomputed when the document changes rather than every frame.
    std::vector<game::content::Issue> mIssues;
    uint64_t mIssuesRevision = ~uint64_t(0);
    std::string mCookStatus = "not cooked";
    RunHandle mPlaytest;
    std::string mExecutablePath; // argv[0], for finding the game binary

    // Material staging mode. A separate scene rather than a panel over the
    // level: the point of a reference stage is that nothing else is in it.
    MaterialStage mStage;
    bool mMaterialMode = false;
    bool mStageAutoSpin = true;
    float mStageSpinSpeed = 0.35f;
    std::vector<std::string> mMaterialNames;
    char mMaterialFilter[64] = {};

    // Particle authoring. The library owns the descs; the panel edits them in
    // place and re-registers, so a change is visible in the viewport on the
    // next frame without a restart.
    eng::ParticleLibrary mParticles;
    int mParticleSelected = -1;
    char mParticleFilter[64] = {};
    std::vector<eng::ParticlesHandle> mParticlePreviews;
    int mFloorVariant = 0; // 0 default grey, 1 dark
    std::string mSelectedMaterial;
    bool mSaveAsOpen = false;
    char mSaveAsPath[512] = {};
    // Set when a discard is waiting on the prompt; consumed by performDiscard.
    bool mDiscardOpen = false;
    Discard mDiscardWhat = Discard::Quit;
    game::content::SceneTemplate mDiscardTemplate =
        game::content::SceneTemplate::Empty;
    char mOutlinerFilter[64] = {};
    bool mOutlinerShowGeometry = true;
    bool mThumbAutoSpin = true;
    bool mCycleMaterials = false;
    std::size_t mCycleIndex = 0;
    EditorCamera mCameraBeforeMode;
};

} // namespace ed
