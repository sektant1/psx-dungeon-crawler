#pragma once

#include "CommandStack.h"
#include "EditorDocument.h"
#include "EditorScene.h"
#include "Gizmo.h"
#include "GizmoTool.h"
#include "Palette.h"
#include "Selection.h"

#include "../EditorCamera.h"

#include <ecs/RendererSceneBackend.h>

#include <eng/Renderer.h> // eng::Renderer::DebugLine (nested type used below)

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eng {
class Engine;
class Renderer;
}

namespace editor {

// The 3D level editor application: owns the ECS scene, the free-fly viewport
// camera, selection/gizmo state, undo stack, and the ImGui panels. Driven once
// per frame by editor_main between Engine::tick() and Engine::renderFrame().
class EditorApp {
public:
    EditorApp(eng::Engine& engine, std::string assetDir);

    void frame(float dt);
    bool wantsQuit() const { return mQuit; }

private:
    void drawPanels();
    void drawToolbar();
    void drawViewport();
    void drawOutliner();
    void drawInspector();
    void drawPalette();

    void handleViewportInput(float dt);
    void handleShortcuts();
    bool pickBounds(entt::entity e, glm::vec3& mn, glm::vec3& mx); // real mesh AABB
    void pickAt(glm::vec2 ndc, bool additive);
    void updateGizmoDrag();
    void frameSelection();
    void deleteSelection();
    void duplicateSelection();

    glm::vec3 spawnPosInFrontOfCamera() const;
    glm::vec3 snapToGrid(glm::vec3 p) const; // no-op unless grid snap is on
    bool selectionCentroid(glm::vec3& out);

    void buildMaterialCatalog();                               // mesh->material map
    std::string materialForMesh(const std::string& objPath) const;
    // Resolve any unresolved MeshRenderer handles from their MeshSource path.
    // The single place mesh handles are turned live (spawn/open/dup/undo/gen).
    void ensureMeshHandles();

    void newScene();
    void generateDungeon();
    void saveMap(const std::string& path);
    void openMap(const std::string& path);
    void launchGame();

    // World ground-plane point (y=0) under the pointer -> terrain grid cell.
    // Returns false if the pointer ray doesn't meet the ground.
    bool pointerCell(glm::vec2 ndc, int& col, int& row) const;
    void paintUnderPointer(glm::vec2 ndc);

    // Which manipulation the viewport's LMB performs (WC3-style tool tabs).
    enum class Tool { Doodad, Terrain };

    eng::Engine& mEngine;
    eng::Renderer& mRenderer;
    std::string mAssetDir;
    eng::ecs::RendererSceneBackend mBackend;
    EditorScene mScene;
    EditorDocument mDoc; // two-layer terrain+doodad document (shares mScene's registry)
    Tool mTool = Tool::Doodad;
    char mBrush = '.';       // active terrain tile for the ground brush
    int mLastPaintCol = -999999, mLastPaintRow = -999999; // de-dupe paint-drag
    Selection mSel;
    CommandStack mStack;
    EditorCamera mCam;
    Palette mPalette;
    GizmoMode mGizmoMode = GizmoMode::Translate;
    bool mUniformScale = true; // scale all axes together (proportional)
    bool mGridSnap = false;    // snap placement + move to a fixed world grid
    float mGridSize = 1.0f;    // grid cell size when mGridSnap is on
    int mGenSeed = 1;          // BSP seed for the Generate button
    std::unordered_map<std::string, std::string> mMatByMesh; // mesh file -> material
    float mSnapStep = 0.0f;
    std::string mMapPath;
    bool mQuit = false;
    int mVpW = 960, mVpH = 640;

    // Viewport-image screen rect (min/size), recorded during drawViewport for
    // mouse->ndc mapping in handleViewportInput.
    glm::vec2 mVpMin{0.0f};
    glm::vec2 mVpSize{0.0f};
    bool mVpHovered = false;
    bool mLooking = false;     // RMB free-look latch
    bool mBuiltLayout = false; // one-time dock layout guard
    int mPendingDestructive = 0; // 1=New 2=Generate 3=Open: awaiting confirm

    // Doodad manipulation (axis hit-test + translate/rotate/scale drag +
    // undoable release). All mid-drag state lives inside GizmoTool.
    GizmoTool mGizmo;
    GizmoConfig gizmoConfig() const; // snapshot the toolbar tunables for a drag

    // Retained per-frame overlay line buffer (grid + selection box + axes);
    // cleared and refilled each frame instead of reallocating.
    std::vector<eng::Renderer::DebugLine> mDebugLines;

    char mPathBuf[512] = {0};
};

} // namespace editor
