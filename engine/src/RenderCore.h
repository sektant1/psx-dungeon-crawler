#pragma once
#include <cstdint>
#include <string>

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
              const std::string& title, const std::string& appAssetDir);
    // Brings up the PSX/Stylized post chain (scene downscale + stylize +
    // dither) if not already active. Idempotent; the chain stays on once up.
    void enablePostChain();
    // Rebuilds the chain with RT sizes = window / pixelSize. Clamped 1..16.
    void setPixelSize(int pixelSize);
    // Material edits on compositor render_quad passes need a chain recompile
    // before the cloned pass material sees the new GPU params.
    void markPostChainDirty();
    void renderFrame(float dt);
    void onResize(int width, int height);
    void writeScreenshot(const std::string& path);
    void shutdown();

    Ogre::SceneManager* sceneMgr() const { return mSceneMgr; }
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
};

} // namespace eng
