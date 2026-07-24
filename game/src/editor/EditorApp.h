#pragma once

#include "CommandStack.h"
#include "EditorScene.h"
#include "Gizmo.h"
#include "Palette.h"
#include "Selection.h"

#include "../EditorCamera.h"

#include <ecs/RendererSceneBackend.h>

#include <glm/glm.hpp>

#include <string>

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
    void pickAt(glm::vec2 ndc, bool additive);
    void updateGizmoDrag();

    glm::vec3 spawnPosInFrontOfCamera() const;
    bool selectionCentroid(glm::vec3& out);

    void newScene();
    void saveMap(const std::string& path);
    void openMap(const std::string& path);
    void launchGame();

    eng::Engine& mEngine;
    eng::Renderer& mRenderer;
    std::string mAssetDir;
    eng::ecs::RendererSceneBackend mBackend;
    EditorScene mScene;
    Selection mSel;
    CommandStack mStack;
    EditorCamera mCam;
    Palette mPalette;
    GizmoMode mGizmoMode = GizmoMode::Translate;
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

    // Gizmo drag state.
    bool mDragging = false;
    int mDragAxis = 0; // 0=x,1=y,2=z
    eng::ecs::Transform mPreDrag;
    glm::vec3 mDragStartHit{0.0f};
    glm::vec3 mDragCentroid{0.0f}; // pivot for rotate/scale
    glm::vec3 mDragStartVec{0.0f}; // rotate reference vector (pivot->pointer)
    float mDragStartT = 0.0f;      // axis param at grab (scale reference)

    char mPathBuf[512] = {0};
};

} // namespace editor
