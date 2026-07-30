#pragma once
#include "Commands.h"
#include "SceneValidate.h"
#include "EditorState.h"
#include "MaterialStage.h"
#include "PreviewBridge.h"
#include "RunGame.h"

#include <eng/app/Application.h>

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
    void applyMaterialToSelection(const std::string& material);
    void setMode(bool material);
    // Selection and manipulation, both driven from inside the viewport panel so
    // they share its rect with ImGuizmo.
    void handleViewportPicking(const eng::FrameContext& f);
    void drawGizmo(const eng::FrameContext& f);
    void drawStageGizmo(const eng::FrameContext& f);
    void runCommand(Command command);
    bool saveScene();
    // F6 / F5: cook the authored scene to a runtime map, and play it.
    bool cookScene(std::string& mapPath);
    void runPlaytest();
    void deleteSelection();
    void duplicateSelection();
    // Placement: the ghost under the cursor, and committing it.
    bool hoveredPlacement(const eng::FrameContext& f,
                          game::content::CellPlacement& cell,
                          game::content::XformAuthor& transform) const;
    void placeAt(const game::content::CellPlacement& cell,
                 const game::content::XformAuthor& transform);
    void drawPlacementGhost(const eng::FrameContext& f);
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

    EditorState mState;
    std::unique_ptr<PreviewBridge> mPreview;
    // Held from onStart. The mode switch and the material panel need the
    // renderer outside a frame callback, and the Engine outlives this app.
    eng::Engine* mEngine = nullptr;
    std::string mPendingScene; // from the command line, opened in onStart
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
    std::vector<std::pair<game::content::AuthorId, game::content::XformAuthor>>
        mDragStart;
    int mGizmoOperation = 0; // 0 translate, 1 rotate, 2 scale

    // Place tool: painting drops one piece per cell/edge the cursor crosses,
    // and the whole drag closes as one undo entry.
    bool mPainting = false;
    std::vector<std::string> mPaintedSlots;
    std::vector<Command> mPaintParts;
    std::vector<game::content::AuthorId> mPaintedIds;
    int mBrushYawQuarters = 0;
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
    int mFloorVariant = 0; // 0 default grey, 1 dark
    std::string mSelectedMaterial;
    bool mThumbAutoSpin = true;
    bool mCycleMaterials = false;
    std::size_t mCycleIndex = 0;
    EditorCamera mCameraBeforeMode;
};

} // namespace ed
