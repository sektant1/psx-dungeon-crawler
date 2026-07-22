#include "RenderCore.h"
#include "eng/Log.h"

#include <Ogre.h>
#include <OgreCompositor.h>
#include <OgreCompositorChain.h>
#include <OgreCompositorManager.h>
#include <OgreImGuiOverlay.h>
#include <OgreOverlayManager.h>
#include <OgreOverlaySystem.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace eng {

RenderCore::~RenderCore() { shutdown(); }

bool RenderCore::init(uintptr_t nativeWindowHandle, int width, int height,
                      const std::string& title, const std::string& appAssetDir,
                      bool vsync)
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
    params["vsync"] = vsync ? "true" : "false";
    mWindow = mRoot->createRenderWindow(title, width, height, false, &params);
    mSceneMgr = mRoot->createSceneManager();

    // Resource locations AFTER the render window exists (material parsing
    // needs a live render system). Engine-owned PSX stack + app assets.
    auto& rgm = Ogre::ResourceGroupManager::getSingleton();
    // Ogre's own media: stencil shadow volume extrusion programs
    // (Ogre/ShadowExtrude*) live in Media/Main and are required the moment
    // a shadow technique is set.
    rgm.addResourceLocation(std::string(OGRE_MEDIA_DIR) + "/Main",
                            "FileSystem", "General");
    const std::string engBase = ENG_ASSET_DIR;
    for (const char* sub : {"/shaders", "/programs", "/materials",
                            "/compositors", "/textures"})
        rgm.addResourceLocation(engBase + sub, "FileSystem", "General");
    for (const char* sub : {"/materials", "/textures", "/textures/props",
                            "/textures/dungeon",
                            "/textures/prototype", "/textures/vfx",
                            "/textures/surfaces", "/particles"}) {
        const std::string dir = appAssetDir + sub;
        if (std::filesystem::is_directory(dir))
            rgm.addResourceLocation(dir, "FileSystem", "General");
    }
    rgm.initialiseAllResourceGroups();

    // The PSX shaders bind 16 light slots (psx_lighting.glsl); Ogre's
    // per-pass default of 8 would truncate the per-renderable light list
    // before the auto params fill those arrays.
    for (const auto& it :
         Ogre::MaterialManager::getSingleton().getResourceIterator()) {
        auto mat = Ogre::static_pointer_cast<Ogre::Material>(it.second);
        for (Ogre::Technique* tech : mat->getTechniques())
            for (Ogre::Pass* pass : tech->getPasses()) {
                pass->setMaxSimultaneousLights(16);
                // OGRE's internal "Warning" texture is easy to mistake for
                // broken lighting. Replace missing file-backed textures with
                // the project's loud PINKY prototype grid and name the bad
                // binding in the log. Compositor/render-target units are not
                // file-backed and are intentionally ignored here.
                for (Ogre::TextureUnitState* unit :
                     pass->getTextureUnitStates()) {
                    if (unit->getContentType() !=
                        Ogre::TextureUnitState::CONTENT_NAMED)
                        continue;
                    const Ogre::String missing = unit->getTextureName();
                    if (missing.empty() ||
                        rgm.resourceExistsInAnyGroup(missing))
                        continue;
                    log::error("Material '%s': texture '%s' is missing; using PINKY.png",
                               mat->getName().c_str(), missing.c_str());
                    unit->setTextureName("PINKY.png");
                }
            }
    }

    // Hard-edged stencil shadows: period-correct (no soft filtering), work
    // with any material, and need no extra shader plumbing. Modulative =
    // one darkening pass over shadowed areas; casters and lights both opt
    // in (Entity/Light setCastShadows via the Renderer API). Must run
    // AFTER resource groups initialise: setting the technique loads Ogre's
    // extrusion programs (Ogre/ShadowExtrude*, Media/Main).
    mSceneMgr->setShadowTechnique(Ogre::SHADOWTYPE_STENCIL_MODULATIVE);
    // Infinite-far-plane shadow volumes (the Ogre default) force the camera
    // far clip to 0/infinity, which zeroes the far_clip_distance auto param.
    // psx.frag uses the far clip for its depth metadata. Infinite volumes
    // would make that value invalid, so use finite volumes instead.
    mSceneMgr->setShadowUseInfiniteFarPlane(false);
    mSceneMgr->setShadowColour(Ogre::ColourValue(0.55f, 0.55f, 0.62f));
    mSceneMgr->setShadowFarDistance(15.0f);

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
    // The ImGuiOverlay ctor created the ImGui context. Enable docking (single
    // OS window: DockSpace only, no multi-viewport -- that would need a platform
    // backend Ogre's overlay renderer doesn't provide). This lets the editor
    // dock panels around the RTT viewport. Persist layout to imgui.ini.
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }
    // show() runs the (private) Overlay::initialise() override, which builds
    // the ImGui font atlas and creates "ImGui/material". Without it the first
    // ImGuiOverlay::NewFrame() dereferences an unbuilt atlas and crashes.
    // Hide again immediately; DebugUi re-shows after its first NewFrame().
    mImGuiOverlay->show();
    mImGuiOverlay->hide();

    // OGRE's ImGui material is fixed-function, which GL3Plus cannot render
    // ("no Vertex Shader ... use RTSS"). Swap in the hand-written GLSL pair
    // so the whole engine stays RTSS-free (imgui.program / imgui.{vert,frag}).
    if (auto imguiMat = Ogre::MaterialManager::getSingleton().getByName(
            "ImGui/material", Ogre::RGN_INTERNAL)) {
        Ogre::Pass* pass = imguiMat->getTechnique(0)->getPass(0);
        pass->setVertexProgram("ImGui/VS");
        pass->setFragmentProgram("ImGui/FS");
    }
    Ogre::OverlayManager::getSingleton().addOverlay(mImGuiOverlay);
    return true;
}

void RenderCore::enablePostChain()
{
    if (!mViewport)
        return;
    auto& cm = Ogre::CompositorManager::getSingleton();
    if (!mChainAdded) {
        // FLOAT16 MRT creation can fail on weak GL drivers; fall back to the
        // raw (post-free) output instead of crashing. Details in ogre.log.
        try {
            cm.addCompositor(mViewport, "PSX/Stylized");
            mChainAdded = true;
        } catch (const Ogre::Exception& e) {
            Ogre::LogManager::getSingleton().logError(
                "PSX/Stylized compositor unavailable, post chain disabled: " +
                e.getDescription());
            return;
        }
    }
    cm.setCompositorEnabled(mViewport, "PSX/Stylized", true);
    mChainEnabled = true;
}

void RenderCore::setPixelSize(int pixelSize)
{
    mPixelSize = std::clamp(pixelSize, 1, 16);
    if (!mViewport)
        return;
    Ogre::CompositorPtr comp =
        Ogre::CompositorManager::getSingleton().getByName("PSX/Stylized");
    if (!comp)
        return;
    // Patch the definition; instances are rebuilt from it on re-add. Scaled
    // (widthFactor/heightFactor) textures also track window resizes for free.
    // Add a hair of upward rounding so the float size derivation truncates to
    // window/pixelSize exactly (e.g. 960 * (1/3) must give 320, not 319).
    const float f = 1.0f / float(mPixelSize) + 1e-6f;
    const float fHalf = 0.5f / float(mPixelSize) + 1e-6f;
    const std::pair<const char*, float> texFactors[] = {
        {"mrt", f}, {"rt_post", f}, {"rt_final", f}, {"rt_resolve", f},
        {"rt_bright", fHalf}, {"rt_blur", fHalf},
    };
    Ogre::CompositionTechnique* tech = comp->getTechnique(0);
    for (auto& [name, factor] : texFactors) {
        auto* def = tech->getTextureDefinition(name);
        if (!def)
            continue; // compositor script and this list drifted apart
        def->widthFactor = factor;
        def->heightFactor = factor;
    }
    // Rebuild whichever chain instances exist so they pick up the new sizes.
    // The window and the editor RTT can each host an independent instance.
    auto& cm = Ogre::CompositorManager::getSingleton();
    if (mOffscreenChain && mOffscreenVp) {
        cm.setCompositorEnabled(mOffscreenVp, "PSX/Stylized", false);
        cm.removeCompositor(mOffscreenVp, "PSX/Stylized");
        mOffscreenChain = false;
        try {
            cm.addCompositor(mOffscreenVp, "PSX/Stylized");
            cm.setCompositorEnabled(mOffscreenVp, "PSX/Stylized", true);
            mOffscreenChain = true;
        } catch (const Ogre::Exception& e) {
            Ogre::LogManager::getSingleton().logError(
                "Editor offscreen post chain re-add failed: " +
                e.getDescription());
        }
    }
    if (mChainAdded) {
        cm.removeCompositor(mViewport, "PSX/Stylized");
        mChainAdded = false;
        if (mChainEnabled)
            enablePostChain(); // re-add
    }
}

void RenderCore::enableOffscreenViewport(int w, int h)
{
    w = std::max(1, w);
    h = std::max(1, h);
    if (mOffscreenTex) {
        resizeOffscreenViewport(w, h);
        return;
    }
    mOffW = w;
    mOffH = h;
    mOffscreenTex = Ogre::TextureManager::getSingleton().createManual(
        "EditorViewportRTT", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
        Ogre::TEX_TYPE_2D, uint32_t(w), uint32_t(h), 0, Ogre::PF_R8G8B8A8,
        Ogre::TU_RENDERTARGET);
    Ogre::RenderTexture* rt = mOffscreenTex->getBuffer()->getRenderTarget();
    rt->setAutoUpdated(true);
    mOffscreenVp = rt->addViewport(mCamera);
    mOffscreenVp->setOverlaysEnabled(false); // keep imgui OUT of the RTT
    mOffscreenVp->setClearEveryFrame(true);
    mOffscreenVp->setBackgroundColour(Ogre::ColourValue(0.02f, 0.02f, 0.03f, 1.0f));

    // Single-compositor arrangement. The PSX chain uses fixed-name MRT textures,
    // so two live "PSX/Stylized" instances collide and blacken both targets.
    // Solution: the chain lives ONLY on the RTT (the editor's real 3D view), and
    // the OS window renders NO scene geometry -- its visibility mask is zeroed so
    // no PSX material is invoked there (an uncomposited PSX pass renders black).
    // The window then only clears dark and draws the imgui overlay; the docked
    // Scene panel shows the fully post-processed RTT.
    auto& cm = Ogre::CompositorManager::getSingleton();
    if (mChainAdded) {
        cm.setCompositorEnabled(mViewport, "PSX/Stylized", false);
        cm.removeCompositor(mViewport, "PSX/Stylized");
        mChainAdded = false;
    }
    mViewport->setVisibilityMask(0);        // window draws no scene geometry
    mViewport->setOverlaysEnabled(true);    // ...but keeps the imgui overlay
    mViewport->setBackgroundColour(Ogre::ColourValue(0.06f, 0.06f, 0.07f, 1.0f));
    try {
        cm.addCompositor(mOffscreenVp, "PSX/Stylized");
        cm.setCompositorEnabled(mOffscreenVp, "PSX/Stylized", true);
        mOffscreenChain = true;
    } catch (const Ogre::Exception& e) {
        Ogre::LogManager::getSingleton().logError(
            "Editor offscreen post chain unavailable: " + e.getDescription());
    }
    // Re-apply the current pixel size so the RTT's PSX targets match.
    setPixelSize(mPixelSize);
}

void RenderCore::resizeOffscreenViewport(int w, int h)
{
    w = std::max(1, w);
    h = std::max(1, h);
    if (!mOffscreenTex || (w == mOffW && h == mOffH))
        return;
    // Tear down the old RTT (removes its viewport + compositor) and rebuild.
    auto& cm = Ogre::CompositorManager::getSingleton();
    if (mOffscreenChain && mOffscreenVp) {
        cm.setCompositorEnabled(mOffscreenVp, "PSX/Stylized", false);
        cm.removeCompositor(mOffscreenVp, "PSX/Stylized");
        mOffscreenChain = false;
    }
    mOffscreenTex->getBuffer()->getRenderTarget()->removeAllViewports();
    Ogre::TextureManager::getSingleton().remove(mOffscreenTex);
    mOffscreenTex.reset();
    mOffscreenVp = nullptr;
    enableOffscreenViewport(w, h); // recreate at the new size
}

uint64_t RenderCore::viewportTextureId() const
{
    if (!mOffscreenTex)
        return 0;
    unsigned int id = 0;
    try {
        mOffscreenTex->getCustomAttribute("GLID", &id);
    } catch (const Ogre::Exception&) {
        return 0;
    }
    return uint64_t(id);
}

void RenderCore::markPostChainDirty()
{
    if (!mViewport || !mChainAdded)
        return;
    Ogre::CompositorChain* chain =
        Ogre::CompositorManager::getSingleton().getCompositorChain(mViewport);
    if (chain)
        chain->_markDirty();
}

void RenderCore::renderFrame(float dt) { mRoot->renderOneFrame(dt); }

void RenderCore::frameStats(size_t& batches, size_t& triangles) const
{
    batches = 0;
    triangles = 0;
    const auto add = [&](Ogre::RenderTarget* rt) {
        if (!rt)
            return;
        const auto& s = rt->getStatistics();
        batches += size_t(s.batchCount);
        triangles += size_t(s.triangleCount);
    };
    add(mWindow);
    if (!mViewport || !mChainAdded)
        return;
    Ogre::CompositorChain* chain =
        Ogre::CompositorManager::getSingleton().getCompositorChain(mViewport);
    if (!chain)
        return;
    // Texture names as declared in psx.compositor (same list setPixelSize
    // patches); the "mrt" entry is the scene pass.
    for (Ogre::CompositorInstance* ci : chain->getCompositorInstances())
        if (ci->getEnabled())
            for (const char* tex :
                 {"mrt", "rt_post", "rt_final", "rt_bright", "rt_blur"})
                add(ci->getRenderTarget(tex));
}

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
    // Tear down the editor offscreen RTT before the scene manager / root go.
    if (mOffscreenTex) {
        auto& cm = Ogre::CompositorManager::getSingleton();
        if (mOffscreenChain && mOffscreenVp) {
            cm.setCompositorEnabled(mOffscreenVp, "PSX/Stylized", false);
            cm.removeCompositor(mOffscreenVp, "PSX/Stylized");
            mOffscreenChain = false;
        }
        mOffscreenTex->getBuffer()->getRenderTarget()->removeAllViewports();
        Ogre::TextureManager::getSingleton().remove(mOffscreenTex);
        mOffscreenTex.reset();
        mOffscreenVp = nullptr;
    }
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
