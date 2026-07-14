#pragma once
#include <cstdint>
#include <string>

namespace Ogre {
class Root;
class RenderWindow;
class SceneManager;
class Camera;
class Viewport;
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
    void setDitherEnabled(bool enabled);
    void renderFrame(float dt);
    void onResize(int width, int height);
    void writeScreenshot(const std::string& path);
    void shutdown();

    Ogre::SceneManager* sceneMgr() const { return mSceneMgr; }
    Ogre::Camera* camera() const { return mCamera; }
    Ogre::Viewport* viewport() const { return mViewport; }

private:
    Ogre::Root* mRoot = nullptr;
    Ogre::RenderWindow* mWindow = nullptr;
    Ogre::SceneManager* mSceneMgr = nullptr;
    Ogre::Camera* mCamera = nullptr;
    Ogre::Viewport* mViewport = nullptr;
    bool mDitherAdded = false;
};

} // namespace eng
