#include <cstdlib>
#include "RenderCore.h"
#include "eng/Log.h"
#include <eng/render/PrototypeAssets.h>

#include <Ogre.h>
#include <OgreCompositor.h>
#include <OgreCompositorChain.h>
#include <OgreCompositorManager.h>

#include <imgui.h>
#include <ImGuizmo.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>

#include <SDL2/SDL.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace eng {
namespace {

} // namespace

RenderCore::~RenderCore() { shutdown(); }

bool RenderCore::init(uintptr_t nativeWindowHandle, void* sdlWindow, int width,
                      int height, const std::string& title,
                      const std::string& appAssetDir, bool vsync)
{
    mSdlWindow = sdlWindow;
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
    // Every directory under both asset roots, discovered rather than listed.
    // Ogre's FileSystem locations are not recursive, so each subdirectory has
    // to be registered on its own; enumerating them means adding a texture
    // folder is a matter of creating it, not of editing and rebuilding the
    // engine. Meshes are loaded by path and are deliberately not part of this.
    const auto addTree = [&rgm](const std::string& root) {
        if (!std::filesystem::is_directory(root))
            return;
        rgm.addResourceLocation(root, "FileSystem", "General");
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                log::error("RenderCore: walking '%s': %s", root.c_str(),
                           ec.message().c_str());
                break;
            }
            if (it->is_directory())
                rgm.addResourceLocation(it->path().string(), "FileSystem",
                                        "General");
        }
    };
    addTree(ENG_ASSET_DIR);
    addTree(appAssetDir);
    // The prototype textures ship as real files in engine/assets/textures, so
    // the material scripts' texture units resolve like any other texture. They
    // used to be createManual'd here before this call, which made the script
    // compiler compare its own declaration against the manual texture and
    // reject the mismatch on every run ("overriding previous declarations of
    // texture ... with different parameters"). Generating them after this call
    // is not an option either: the declarations would find no file and group
    // initialisation would fail, taking the rest of the group's resources with
    // it. A file on disk is what both halves of that bind actually want.
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
                    std::string materialName = mat->getName();
                    std::transform(materialName.begin(), materialName.end(),
                                   materialName.begin(),
                                   [](unsigned char c) { return char(std::tolower(c)); });
                    const char* fallback =
                        materialName.find("particle") != std::string::npos
                            ? prototype::kParticleTexture
                            : prototype::kSurfaceTexture;
                    log::error("Material '%s': texture '%s' is missing; using %s",
                               mat->getName().c_str(), missing.c_str(), fallback);
                    unit->setTextureName(fallback);
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
    mSceneMgr->setShadowFarDistance(10.0f);

    mCamera = mSceneMgr->createCamera("MainCamera");
    mCamera->setFOVy(Ogre::Degree(70.0f)); // defaults; app overrides via API
    mCamera->setNearClipDistance(0.05f);
    mCamera->setFarClipDistance(4000.0f);
    mCamera->setAutoAspectRatio(true);

    mViewport = mWindow->addViewport(mCamera);
    mViewport->setBackgroundColour(Ogre::ColourValue::Black);

    // Engine-owned Dear ImGui: our own context + the official SDL2 + OpenGL3
    // backends, built against the vendored imgui (NOT Ogre's ImGuiOverlay,
    // whose imgui copy has a mismatched ABI -> flicker + EndFrame segfault).
    // The same context's font atlas also backs world-label text sprites
    // (Renderer::attachTextSprite). Ogre's GL context is current here, so the
    // GL3 loader in ImGui_ImplOpenGL3_Init() resolves against it.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // no imgui.ini persistence
    io.DisplaySize = ImVec2(float(width), float(height));
    ImGui::StyleColorsDark();
    if (mSdlWindow)
        ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(mSdlWindow),
                                     nullptr);
    ImGui_ImplOpenGL3_Init("#version 150");
    // Force the font atlas to build now (text sprites read it immediately, and
    // ImGui_ImplOpenGL3 uploads it lazily on first render otherwise).
    ImGui_ImplOpenGL3_CreateFontsTexture();
    // Paint imgui after the window RT finishes its scene + post chain.
    mWindow->addListener(this);
    mImGuiInit = true;
    return true;
}

void RenderCore::beginImGuiFrame(float /*dt*/)
{
    if (!mImGuiInit)
        return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    mImGuiFrameStarted = true;
}

void RenderCore::postRenderTargetUpdate(const Ogre::RenderTargetEvent&)
{
    // Called during renderOneFrame after the window's viewport has drawn, before
    // the buffer swap. Blit the imgui draw data on top. ImGui_ImplOpenGL3 backs
    // up and restores all GL state it touches, so Ogre's GL state cache stays
    // coherent for the next frame.
    if (mImGuiRendered)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
    mTargetW = mTargetH = 0; // divisor mode wins; drop any absolute target
    applyChainSizes();
}

void RenderCore::setRenderResolution(int width, int height)
{
    // 0 in either axis means "go back to the window/pixelSize divisor".
    if (width <= 0 || height <= 0) {
        mTargetW = mTargetH = 0;
    } else {
        mTargetW = std::clamp(width, 64, 4096);
        mTargetH = std::clamp(height, 64, 4096);
    }
    applyChainSizes();
}

void RenderCore::applyChainSizes()
{
    if (!mViewport)
        return;
    Ogre::CompositorPtr comp =
        Ogre::CompositorManager::getSingleton().getByName("PSX/Stylized");
    if (!comp)
        return;
    // Patch the definition; instances are rebuilt from it on re-add. Scaled
    // (widthFactor/heightFactor) textures also track window resizes for free.
    // Add a hair of upward rounding so the float size derivation truncates to
    // the intended size exactly (e.g. 960 * (1/3) must give 320, not 319).
    float fx = 1.0f / float(mPixelSize) + 1e-6f;
    float fy = fx;
    if (mTargetW > 0) {
        // Absolute target: the console profiles want their real framebuffer
        // (PS2 640x448, GameCube 640x480), not a fraction of whatever window
        // the player happens to have. Per-axis factors, so the target holds its
        // own aspect regardless of the window's -- Ogre keeps widthFactor and
        // heightFactor separate for exactly this.
        fx = float(mTargetW) / float(mViewport->getActualWidth()) + 1e-6f;
        fy = float(mTargetH) / float(mViewport->getActualHeight()) + 1e-6f;
    }
    const std::pair<const char*, float> texFactors[] = {
        {"mrt", 1.0f}, {"rt_post", 1.0f}, {"rt_final", 1.0f},
        {"rt_resolve", 1.0f}, {"rt_bright", 0.5f}, {"rt_blur", 0.5f},
    };
    Ogre::CompositionTechnique* tech = comp->getTechnique(0);
    for (auto& [name, scale] : texFactors) {
        auto* def = tech->getTextureDefinition(name);
        if (!def)
            continue; // compositor script and this list drifted apart
        def->widthFactor = fx * scale;
        def->heightFactor = fy * scale;
    }
    // Rebuild the window's chain instance so it picks up the new sizes. The
    // editor RTT deliberately runs NO post chain (a clean, Godot-like scene
    // preview), so only the window instance needs rebuilding here.
    auto& cm = Ogre::CompositorManager::getSingleton();
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

    // Dedicated free-fly editor eye, independent of the game/window MainCamera.
    // The app drives it every frame via setEditorCameraPose(). This is what
    // makes the Scene panel a live, navigable viewport (Godot-style) rather than
    // a mirror of some other camera.
    if (!mEditorCam) {
        mEditorCam = mSceneMgr->createCamera("EditorViewportCamera");
        mEditorCam->setNearClipDistance(0.05f);
        mEditorCam->setFarClipDistance(4000.0f);
        mEditorCam->setAutoAspectRatio(false);
        // Ogre 14 cameras carry no transform of their own; a SceneNode does.
        mEditorCamNode = mSceneMgr->getRootSceneNode()->createChildSceneNode();
        mEditorCamNode->attachObject(mEditorCam);
    }
    mEditorCam->setAspectRatio(float(w) / float(h));

    mOffscreenTex = Ogre::TextureManager::getSingleton().createManual(
        "EditorViewportRTT", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
        Ogre::TEX_TYPE_2D, uint32_t(w), uint32_t(h), 0, Ogre::PF_R8G8B8A8,
        Ogre::TU_RENDERTARGET);
    Ogre::RenderTexture* rt = mOffscreenTex->getBuffer()->getRenderTarget();
    rt->setAutoUpdated(true); // updated automatically before the window each frame
    mOffscreenVp = rt->addViewport(mEditorCam);
    mOffscreenVp->setOverlaysEnabled(false); // keep imgui OUT of the RTT
    mOffscreenVp->setClearEveryFrame(true);
    mOffscreenVp->setBackgroundColour(Ogre::ColourValue(0.10f, 0.11f, 0.13f, 1.0f));
    // The preview sphere belongs to the thumbnail target alone.
    mOffscreenVp->setVisibilityMask(kWorldVisibilityMask);

    // NOTE: the editor RTT runs NO PSX compositor. The chain uses fixed-name MRT
    // textures, so a second live "PSX/Stylized" instance would collide with the
    // window's and blacken both. A plain forward render also matches how DCC/
    // engine editors show an unstylized working preview. The window keeps its own
    // PSX chain untouched (it's hidden behind the imgui dockspace, harmless).
}

void RenderCore::setEditorCameraPose(float px, float py, float pz,
                                     float qw, float qx, float qy, float qz,
                                     float fovDeg)
{
    if (!mEditorCam || !mEditorCamNode)
        return;
    mEditorCamNode->setPosition(px, py, pz);
    mEditorCamNode->setOrientation(Ogre::Quaternion(qw, qx, qy, qz));
    mEditorCam->setFOVy(Ogre::Degree(fovDeg));
}

void RenderCore::resizeOffscreenViewport(int w, int h)
{
    w = std::max(1, w);
    h = std::max(1, h);
    if (!mOffscreenTex || (w == mOffW && h == mOffH))
        return;
    // Tear down the old RTT (removes its viewport) and rebuild at the new size.
    // The dedicated editor camera survives across resizes.
    mOffscreenTex->getBuffer()->getRenderTarget()->removeAllViewports();
    Ogre::TextureManager::getSingleton().remove(mOffscreenTex);
    mOffscreenTex.reset();
    mOffscreenVp = nullptr;
    enableOffscreenViewport(w, h); // recreate at the new size
}

void RenderCore::enableThumbnailViewport(int size)
{
    if (mThumbTex)
        return;
    size = std::max(32, size);

    mThumbCam = mSceneMgr->createCamera("MaterialThumbnailCamera");
    mThumbCam->setNearClipDistance(0.05f);
    mThumbCam->setFarClipDistance(100.0f);
    mThumbCam->setAutoAspectRatio(false);
    mThumbCam->setAspectRatio(1.0f); // square, like the thumbnail
    mThumbCamNode = mSceneMgr->getRootSceneNode()->createChildSceneNode();
    mThumbCamNode->attachObject(mThumbCam);

    mThumbTex = Ogre::TextureManager::getSingleton().createManual(
        "MaterialThumbnailRTT", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
        Ogre::TEX_TYPE_2D, uint32_t(size), uint32_t(size), 0, Ogre::PF_R8G8B8A8,
        Ogre::TU_RENDERTARGET);
    Ogre::RenderTexture* rt = mThumbTex->getBuffer()->getRenderTarget();
    rt->setAutoUpdated(true);
    mThumbVp = rt->addViewport(mThumbCam);
    mThumbVp->setOverlaysEnabled(false);
    mThumbVp->setClearEveryFrame(true);
    mThumbVp->setBackgroundColour(Ogre::ColourValue(0.13f, 0.14f, 0.16f, 1.0f));
    // ONLY the preview subject. Without this the thumbnail would show whatever
    // level happens to be loaded behind it.
    mThumbVp->setVisibilityMask(kThumbnailVisibilityFlag);

    // ...and conversely, keep the preview sphere out of every other view.
    if (mViewport)
        mViewport->setVisibilityMask(mViewport->getVisibilityMask() &
                                     ~kThumbnailVisibilityFlag);
    if (mOffscreenVp)
        mOffscreenVp->setVisibilityMask(mOffscreenVp->getVisibilityMask() &
                                        ~kThumbnailVisibilityFlag);
}

void RenderCore::setThumbnailCameraPose(float px, float py, float pz, float qw,
                                        float qx, float qy, float qz,
                                        float fovDeg)
{
    if (!mThumbCam || !mThumbCamNode)
        return;
    mThumbCamNode->setPosition(px, py, pz);
    mThumbCamNode->setOrientation(Ogre::Quaternion(qw, qx, qy, qz));
    mThumbCam->setFOVy(Ogre::Degree(fovDeg));
}

uint64_t RenderCore::thumbnailTextureId() const
{
    if (!mThumbTex)
        return 0;
    unsigned int glId = 0;
    mThumbTex->getCustomAttribute("GLID", &glId);
    return uint64_t(glId);
}

void RenderCore::setOffscreenBackground(float r, float g, float b)
{
    if (mOffscreenVp)
        mOffscreenVp->setBackgroundColour(Ogre::ColourValue(r, g, b, 1.0f));
}

uint64_t RenderCore::viewportTextureId() const
{
    if (!mOffscreenTex)
        return 0;
    // The raw GL texture name, because our ImGui runs on the vendored
    // SDL2/OpenGL3 backend (see beginImGuiFrame), where ImTextureID *is* the GL
    // id. It used to return the Ogre ResourceHandle, which is what OGRE's own
    // ImGuiOverlay wants -- but nothing here uses that overlay, so the handle
    // was silently interpreted as a GL name and the editor viewport drew
    // whatever texture happened to own that id (in practice, the font atlas).
    unsigned int glId = 0;
    mOffscreenTex->getCustomAttribute("GLID", &glId);
    return uint64_t(glId);
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

void RenderCore::renderFrame(float dt)
{
    // Close the imgui frame before Ogre renders so GetDrawData() is valid inside
    // postRenderTargetUpdate. If the app didn't start a frame (e.g. a headless
    // tool), skip Render entirely -- the listener then blits nothing.
    if (mImGuiFrameStarted) {
        ImGui::Render();
        mImGuiRendered = true;
    }
    mRoot->renderOneFrame(dt);
    mImGuiFrameStarted = false;
    mImGuiRendered = false;
}

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
    // Divisor mode needs nothing here: Ogre rescales the chain's targets from
    // the stored factors on its own. An absolute target does need it, because
    // those same factors are relative to the window that was current when they
    // were computed -- left alone, a resize would silently rescale a "640x448"
    // buffer to something else.
    if (mTargetW > 0)
        applyChainSizes();
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
        mOffscreenTex->getBuffer()->getRenderTarget()->removeAllViewports();
        Ogre::TextureManager::getSingleton().remove(mOffscreenTex);
        mOffscreenTex.reset();
        mOffscreenVp = nullptr;
    }
    if (mViewport && mWindow)
        Ogre::CompositorManager::getSingleton().removeCompositorChain(mViewport);
    // Tear down imgui while Ogre's GL context is still current (the GL3 backend
    // deletes GL objects), before the scene manager / root go.
    if (mImGuiInit) {
        if (mWindow)
            mWindow->removeListener(this);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        mImGuiInit = false;
    }
    if (mSceneMgr)
        mRoot->destroySceneManager(mSceneMgr);
    delete mRoot; // last: tears down window, render system, resource managers
    mRoot = nullptr;
    mWindow = nullptr;
    mSceneMgr = nullptr;
    mCamera = nullptr;
    mViewport = nullptr;
}

} // namespace eng
