#include "RenderCore.h"

#include <Ogre.h>
#include <OgreCompositorManager.h>
#include <OgreImGuiOverlay.h>
#include <OgreOverlayManager.h>
#include <OgreOverlaySystem.h>

#include <filesystem>
#include <string>

namespace eng {

RenderCore::~RenderCore() { shutdown(); }

bool RenderCore::init(uintptr_t nativeWindowHandle, int width, int height,
                      const std::string& title, const std::string& appAssetDir)
{
    // Fully programmatic setup: no plugins.cfg / ogre.cfg, no RTSS -- all
    // materials are hand-written GLSL.
    mRoot = new Ogre::Root("", "", "ogre.log");
    mRoot->loadPlugin(std::string(OGRE_PLUGIN_DIR) + "/RenderSystem_GL3Plus");
    mRoot->loadPlugin(std::string(OGRE_PLUGIN_DIR) + "/Plugin_ParticleFX");
    mRoot->loadPlugin(std::string(OGRE_PLUGIN_DIR) + "/Codec_STBI"); // PNG
    mRoot->setRenderSystem(mRoot->getAvailableRenderers().front());
    mRoot->initialise(false); // the engine owns the window

    Ogre::NameValuePairList params;
    params["externalWindowHandle"] = std::to_string(nativeWindowHandle);
    params["vsync"] = "true";
    mWindow = mRoot->createRenderWindow(title, width, height, false, &params);
    mSceneMgr = mRoot->createSceneManager();

    // Resource locations AFTER the render window exists (material parsing
    // needs a live render system). Engine-owned PSX stack + app assets.
    auto& rgm = Ogre::ResourceGroupManager::getSingleton();
    const std::string engBase = ENG_ASSET_DIR;
    for (const char* sub : {"/shaders", "/programs", "/materials",
                            "/compositors", "/textures"})
        rgm.addResourceLocation(engBase + sub, "FileSystem", "General");
    for (const char* sub : {"/materials", "/textures", "/particles"}) {
        const std::string dir = appAssetDir + sub;
        if (std::filesystem::is_directory(dir))
            rgm.addResourceLocation(dir, "FileSystem", "General");
    }
    rgm.initialiseAllResourceGroups();

    mCamera = mSceneMgr->createCamera("MainCamera");
    mCamera->setFOVy(Ogre::Degree(70.0f)); // defaults; app overrides via API
    mCamera->setNearClipDistance(0.05f);
    mCamera->setFarClipDistance(4000.0f);
    mCamera->setAutoAspectRatio(true);

    mViewport = mWindow->addViewport(mCamera);
    mViewport->setBackgroundColour(Ogre::ColourValue::Black);

    // Debug UI: ImGui renders through the overlay queue. The ImGuiOverlay
    // constructor creates the ImGui context; OverlayManager owns the overlay.
    mOverlaySystem = new Ogre::OverlaySystem();
    mSceneMgr->addRenderQueueListener(mOverlaySystem);
    mImGuiOverlay = new Ogre::ImGuiOverlay();
    mImGuiOverlay->setZOrder(300);
    mImGuiOverlay->hide(); // shown by DebugUi after the first NewFrame()
    Ogre::OverlayManager::getSingleton().addOverlay(mImGuiOverlay);
    return true;
}

void RenderCore::setDitherEnabled(bool enabled)
{
    auto& cm = Ogre::CompositorManager::getSingleton();
    if (enabled && !mDitherAdded) {
        cm.addCompositor(mViewport, "PSX/Dither");
        mDitherAdded = true;
    }
    if (mDitherAdded)
        cm.setCompositorEnabled(mViewport, "PSX/Dither", enabled);
}

void RenderCore::renderFrame(float dt) { mRoot->renderOneFrame(dt); }

void RenderCore::onResize(int width, int height)
{
    if (!mWindow)
        return;
    mWindow->resize(width, height);
    mWindow->windowMovedOrResized();
}

void RenderCore::writeScreenshot(const std::string& path)
{
    if (mWindow)
        mWindow->writeContentsToFile(path);
}

void RenderCore::shutdown()
{
    if (!mRoot)
        return;
    if (mViewport && mWindow)
        Ogre::CompositorManager::getSingleton().removeCompositorChain(mViewport);
    if (mSceneMgr && mOverlaySystem)
        mSceneMgr->removeRenderQueueListener(mOverlaySystem);
    if (mSceneMgr)
        mRoot->destroySceneManager(mSceneMgr);
    delete mOverlaySystem; // destroys OverlayManager + ImGuiOverlay (+ImGui ctx)
    mOverlaySystem = nullptr;
    mImGuiOverlay = nullptr;
    delete mRoot; // last: tears down window, render system, resource managers
    mRoot = nullptr;
    mWindow = nullptr;
    mSceneMgr = nullptr;
    mCamera = nullptr;
    mViewport = nullptr;
}

} // namespace eng
