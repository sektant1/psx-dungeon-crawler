#pragma once
#include <cstdint>
#include <string>

#include <OgreTexture.h> // Ogre::TexturePtr member

namespace Ogre {
class Root;
class RenderWindow;
class SceneManager;
class Camera;
class Viewport;
class OverlaySystem;
class ImGuiOverlay;
} // namespace Ogre

namespace eng {

// Internal: owns Ogre::Root and hides Ogre lifetime rules. Root is the first
// Ogre object created and the last destroyed.
class RenderCore
{
public:
    ~RenderCore(); // calls shutdown(); safe if already shut down
    bool init(uintptr_t nativeWindowHandle, int width, int height,
              const std::string& title, const std::string& appAssetDir,
              bool vsync);
    // Brings up the PSX post chain (scene downscale + bloom + dither) if not
    // already active. Idempotent; the chain stays on once up.
    void enablePostChain();
    // Rebuilds the chain with RT sizes = window / pixelSize. Clamped 1..16.
    void setPixelSize(int pixelSize);
    // Material edits on compositor render_quad passes need a chain recompile
    // before the cloned pass material sees the new GPU params.
    void markPostChainDirty();

    // Editor offscreen viewport: render the scene + PSX post chain into a texture
    // (overlays OFF so imgui isn't baked in) shown via ImGui::Image. The window
    // viewport then presents only the imgui panels.
    void enableOffscreenViewport(int w, int h);
    void resizeOffscreenViewport(int w, int h);
    uint64_t viewportTextureId() const;   // GL id for ImGui::Image, 0 if none
    bool offscreenActive() const { return mOffscreenTex.get() != nullptr; }
    // Drive the editor RTT's dedicated camera (a free-fly editor eye, decoupled
    // from the game/window MainCamera). Quaternion is (w,x,y,z).
    void setEditorCameraPose(float px, float py, float pz,
                             float qw, float qx, float qy, float qz,
                             float fovDeg);

    void renderFrame(float dt);
    void onResize(int width, int height);
    void writeScreenshot(const std::string& path);
    void shutdown();

    // Whole-frame batch/triangle counts: window + every live post-chain
    // target (the scene renders into the compositor MRT, which the window's
    // own statistics never see).
    void frameStats(size_t& batches, size_t& triangles) const;

    Ogre::SceneManager* sceneMgr() const { return mSceneMgr; }
    Ogre::Root* root() const { return mRoot; }
    Ogre::Camera* camera() const { return mCamera; }
    Ogre::Viewport* viewport() const { return mViewport; }
    Ogre::RenderWindow* window() const { return mWindow; }
    Ogre::ImGuiOverlay* imguiOverlay() const { return mImGuiOverlay; }

private:
    Ogre::Root* mRoot = nullptr;
    Ogre::RenderWindow* mWindow = nullptr;
    Ogre::SceneManager* mSceneMgr = nullptr;
    Ogre::Camera* mCamera = nullptr;
    Ogre::Viewport* mViewport = nullptr;
    bool mChainAdded = false;   // compositor instance exists on the viewport
    bool mChainEnabled = false; // chain was ever requested (one-way; gates the
                                // setPixelSize re-add on cold start)
    int mPixelSize = 3;
    Ogre::OverlaySystem* mOverlaySystem = nullptr; // deleted before Root
    Ogre::ImGuiOverlay* mImGuiOverlay = nullptr;   // owned by OverlayManager

    // Editor offscreen RTT (scene + post baked into a texture for ImGui::Image).
    Ogre::TexturePtr mOffscreenTex;
    Ogre::Viewport* mOffscreenVp = nullptr;
    Ogre::Camera* mEditorCam = nullptr;    // dedicated free-fly eye for the RTT
    Ogre::SceneNode* mEditorCamNode = nullptr; // carries mEditorCam's transform
    int mOffW = 0, mOffH = 0;
};

} // namespace eng
