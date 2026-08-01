#include <cstdlib>
#include "RenderCore.h"
#include "eng/Log.h"
#include <eng/assets/AssetRoot.h>
#include <eng/render/ImGuiHint.h>
#include <eng/render/ImGuiTheme.h>
#include <eng/render/PrototypeAssets.h>
#include <eng/render/ImGuiLayout.h>

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

// Ogre logs the failures that matter most to this engine -- "has no supportable
// Techniques", GLSL compile output, missing resources, compositor rejections --
// and it logs them to ogre.log, where the console could not see them. Every one
// of those bugs then presented as a blank material with a clean console.
//
// So Ogre's log is mirrored into eng::log, which is what the console listens
// to. It is a listener rather than a replacement: ogre.log keeps its copy, and
// the message goes on to Ogre's own file writer untouched.
class OgreLogBridge final : public Ogre::LogListener {
public:
    void messageLogged(const Ogre::String& message, Ogre::LogMessageLevel level,
                       bool /*maskDebug*/, const Ogre::String& /*logName*/,
                       bool& /*skipThisMessage*/) override
    {
        // Ogre's own lines are already formatted: pass them as a literal
        // argument, or a message containing a '%' reformats as garbage.
        switch (level) {
        case Ogre::LML_CRITICAL:
            log::error("Ogre: %s", message.c_str());
            break;
        case Ogre::LML_WARNING:
            log::warn("Ogre: %s", message.c_str());
            break;
        default:
            // Trivial/normal Ogre chatter is voluminous (every resource, every
            // parse). It stays in ogre.log; the console gets what went wrong.
            break;
        }
    }
};

OgreLogBridge& ogreLogBridge()
{
    static OgreLogBridge bridge;
    return bridge;
}

} // namespace

RenderCore::~RenderCore()
{
    shutdown();
}

bool RenderCore::init(uintptr_t nativeWindowHandle, void* sdlWindow, int width,
                      int height, const std::string& title, bool vsync)
{
    mSdlWindow = sdlWindow;
    // Fully programmatic setup: no plugins.cfg / ogre.cfg, no RTSS -- all
    // materials are hand-written GLSL.
    mRoot = new Ogre::Root("", "", "ogre.log");
    // Attached immediately after Root creates the default log, so plugin load
    // and render-system startup failures are already mirrored to the console.
    if (Ogre::Log* ogreLog = Ogre::LogManager::getSingleton().getDefaultLog())
        ogreLog->addListener(&ogreLogBridge());
    mRoot->loadPlugin(std::string(OGRE_PLUGIN_DIR) + "/RenderSystem_GL3Plus");
    // D12 -- one particle stack, the engine's. Plugin_ParticleFX is
    // deliberately NOT loaded: nothing calls createParticleSystem, the one
    // ParticleFX script that existed (sparkle.particle) was orphaned and is
    // gone, and eng's own particles are Ogre::BillboardSet. OGRE still *builds*
    // the plugin (OGRE_BUILD_PLUGIN_PFX in cmake/Dependencies.cmake:161 and
    // :278) because flipping that forces a multi-minute reconfigure of a
    // from-source dependency; an unloaded plugin costs nothing at runtime.
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
    rgm.addResourceLocation(std::string(OGRE_MEDIA_DIR) + "/Main", "FileSystem",
                            "General");
    // The content packs the app mounted, in priority order -- and only the
    // directories the manifest declares `resources`. Ogre's FileSystem
    // locations are not recursive, so each one is registered on its own; the
    // list comes from assets.toml rather than from a recursive walk, because a
    // walk registers whatever authoring folder happens to be sitting under a
    // root. Meshes, configs and scenes are read by path and are deliberately
    // not here.
    const std::vector<std::filesystem::path> resourceDirs =
        assets::resourceDirs();
    if (resourceDirs.empty())
        log::error("RenderCore: no content mounted; every material and texture "
                   "will be missing");
    for (const std::filesystem::path& dir : resourceDirs)
        rgm.addResourceLocation(dir.string(), "FileSystem", "General");
    // The prototype textures ship as real files in assets/engine/textures, so
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
                                   materialName.begin(), [](unsigned char c) {
                                       return char(std::tolower(c));
                                   });
                    const char* fallback =
                        materialName.find("particle") != std::string::npos
                            ? prototype::kParticleTexture
                            : prototype::kSurfaceTexture;
                    log::error(
                        "Material '%s': texture '%s' is missing; using %s",
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
    io.DisplaySize = ImVec2(float(width), float(height));
    // Docking, for every app on this context: the tool UI is a set of panels
    // the viewer arranges, not one fixed column. DebugTools builds a default
    // layout the first time it draws.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable |
                      ImGuiConfigFlags_NavEnableKeyboard |
                      ImGuiConfigFlags_NavEnableGamepad;
    // Layout persistence -- and ONLY layout; see eng/render/ImGuiLayout.h for
    // what the ini holds and for the single switch that turns it off. This has
    // to run before the first NewFrame, because that is when imgui would
    // otherwise load the file itself.
    imgui_layout::install();
    // Two fonts, in this order on purpose.
    //
    // Fonts[0] stays the imgui built-in: Renderer::attachTextSprite blits
    // glyphs out of Fonts[0] for world-space labels, and swapping the face
    // under it would change the rendered world image, which is frozen.
    //
    // Fonts[1] is the vendored DejaVu Sans Mono, and it is the *default* for
    // every tool window. ProggyClean is a 13px bitmap face: it aliases badly
    // and cannot be scaled, which is what made the editor look ragged.
    io.Fonts->AddFontDefault();
    ImFontConfig toolFont;
    toolFont.OversampleH = 2;
    toolFont.OversampleV = 2;
    toolFont.PixelSnapH = false;
    const std::string monoPath =
        assets::resolve("fonts/DejaVuSansMono.ttf").string();
    if (ImFont* mono = monoPath.empty()
                           ? nullptr
                           : io.Fonts->AddFontFromFileTTF(monoPath.c_str(),
                                                          15.0f, &toolFont))
        io.FontDefault = mono;
    else
        eng::log::warn("RenderCore: DejaVuSansMono.ttf missing; "
                       "tool UI falls back to the imgui built-in font");
    // PSX_IMGUI_THEME picks a registered theme by id; unknown ids fall back to
    // the engine default instead of failing the render init.
    const char* themeEnv = std::getenv("PSX_IMGUI_THEME");
    if (!themeEnv || !imguitheme::apply(themeEnv))
        imguitheme::apply("hud_reliquary");
    if (mSdlWindow)
        ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(mSdlWindow),
                                     nullptr);
    ImGui_ImplOpenGL3_Init("#version 150");
    // Force the font atlas to build now (text sprites read it immediately, and
    // ImGui_ImplOpenGL3 uploads it lazily on first render otherwise).
    ImGui_ImplOpenGL3_CreateFontsTexture();
    // Hover help for every tool app, loaded once against the same context.
    imguihint::load(assets::resolve("ui/hints.toml").string());
    // Paint imgui after the window RT finishes its scene + post chain.
    mWindow->addListener(this);
    mImGuiInit = true;

    // What actually came up. This is the first question asked of any render
    // bug -- which GPU, which driver, what size, how many materials parsed --
    // and every answer used to live in ogre.log, a file nobody opens until
    // they already suspect the renderer. Four lines, once, at boot.
    if (const Ogre::RenderSystemCapabilities* caps =
            mRoot->getRenderSystem()->getCapabilities()) {
        log::info("GPU: %s", caps->getDeviceName().c_str());
        log::info("GPU: %s, GLSL %d, %d texture units, %d MRT",
                  mRoot->getRenderSystem()->getName().c_str(),
                  int(caps->getDriverVersion().major * 100 +
                      caps->getDriverVersion().minor),
                  int(caps->getNumTextureUnits()),
                  int(caps->getNumMultiRenderTargets()));
    }
    log::info("Window: %ux%u, vsync %s", mWindow->getWidth(),
              mWindow->getHeight(), vsync ? "on" : "off");
    // Ogre's resource iterators are map iterators, not random access, so the
    // counts are walked rather than subtracted.
    const auto countResources = [](auto& manager) {
        std::size_t n = 0;
        for (const auto& unused : manager.getResourceIterator()) {
            (void)unused;
            ++n;
        }
        return n;
    };
    std::string mountList;
    for (const assets::Pack& pack : assets::mounted())
        mountList += (mountList.empty() ? "" : " + ") + pack.id;
    log::info(
        "Assets: %zu materials, %zu textures parsed from %zu dirs of [%s]",
        countResources(Ogre::MaterialManager::getSingleton()),
        countResources(Ogre::TextureManager::getSingleton()),
        resourceDirs.size(),
        mountList.empty() ? "(nothing mounted)" : mountList.c_str());
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
    // Called during renderOneFrame after the window's viewport has drawn,
    // before the buffer swap. Blit the imgui draw data on top.
    // ImGui_ImplOpenGL3 backs up and restores all GL state it touches, so
    // Ogre's GL state cache stays coherent for the next frame.
    if (mImGuiRendered)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void RenderCore::enablePostChain()
{
    if (!mViewport)
        return;
    // PSX_NO_POST=1 renders straight to the window with no post chain. A
    // bisecting switch: when the image is wrong, this answers "is it the scene
    // or is it the chain" in one run instead of a rebuild.
    if (std::getenv("PSX_NO_POST")) {
        Ogre::LogManager::getSingleton().logMessage(
            "PSX_NO_POST set: post chain disabled");
        return;
    }
    auto& cm = Ogre::CompositorManager::getSingleton();
    if (!mChainAdded) {
        // FLOAT16 MRT creation can fail on weak GL drivers; fall back to the
        // raw (post-free) output instead of crashing. Details in ogre.log.
        try {
            cm.addCompositor(mViewport, "PSX/Stylized");
            mChainAdded = true;
        }
        catch (const Ogre::Exception& e) {
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
    }
    else {
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
        {"mrt", 1.0f},        {"rt_post", 1.0f},   {"rt_final", 1.0f},
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
    // makes the Scene panel a live, navigable viewport (Godot-style) rather
    // than a mirror of some other camera.
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
        "EditorViewportRTT",
        Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
        Ogre::TEX_TYPE_2D, uint32_t(w), uint32_t(h), 0, Ogre::PF_R8G8B8A8,
        Ogre::TU_RENDERTARGET);
    Ogre::RenderTexture* rt = mOffscreenTex->getBuffer()->getRenderTarget();
    rt->setAutoUpdated(
        true); // updated automatically before the window each frame
    mOffscreenVp = rt->addViewport(mEditorCam);
    mOffscreenVp->setOverlaysEnabled(false); // keep imgui OUT of the RTT
    mOffscreenVp->setClearEveryFrame(true);
    mOffscreenVp->setBackgroundColour(
        Ogre::ColourValue(0.10f, 0.11f, 0.13f, 1.0f));
    // The preview sphere belongs to the thumbnail target alone.
    mOffscreenVp->setVisibilityMask(kWorldVisibilityMask);

    // NOTE: the editor RTT runs NO PSX compositor. The chain uses fixed-name
    // MRT textures, so a second live "PSX/Stylized" instance would collide with
    // the window's and blacken both. A plain forward render also matches how
    // DCC/ engine editors show an unstylized working preview. The window keeps
    // its own PSX chain untouched (it's hidden behind the imgui dockspace,
    // harmless).
}

void RenderCore::setEditorCameraPose(float px, float py, float pz, float qw,
                                     float qx, float qy, float qz, float fovDeg)
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
        "MaterialThumbnailRTT",
        Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
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
    // Close the imgui frame before Ogre renders so GetDrawData() is valid
    // inside postRenderTargetUpdate. If the app didn't start a frame (e.g. a
    // headless tool), skip Render entirely -- the listener then blits nothing.
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
    // Tear down the offscreen RTTs before the scene manager / root go. Both of
    // them: a render target outliving its SceneManager crashes on the way out,
    // and the crash lands in plugin teardown where nothing points at the cause.
    if (mOffscreenTex) {
        mOffscreenTex->getBuffer()->getRenderTarget()->removeAllViewports();
        Ogre::TextureManager::getSingleton().remove(mOffscreenTex);
        mOffscreenTex.reset();
        mOffscreenVp = nullptr;
    }
    if (mThumbTex) {
        mThumbTex->getBuffer()->getRenderTarget()->removeAllViewports();
        Ogre::TextureManager::getSingleton().remove(mThumbTex);
        mThumbTex.reset();
        mThumbVp = nullptr;
        mThumbCam = nullptr;
        mThumbCamNode = nullptr;
    }
    if (mViewport && mWindow)
        Ogre::CompositorManager::getSingleton().removeCompositorChain(
            mViewport);
    // Tear down imgui while Ogre's GL context is still current (the GL3 backend
    // deletes GL objects), before the scene manager / root go.
    if (mImGuiInit) {
        // Before the context goes: imgui's own save timer runs every few
        // seconds, so without this the last arrangement of a session is lost
        // whenever the app is closed inside that window -- which reads as the
        // feature not working at all.
        imgui_layout::save();
        if (mWindow)
            mWindow->removeListener(this);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        mImGuiInit = false;
    }
    if (mSceneMgr)
        mRoot->destroySceneManager(mSceneMgr);
    // Detached before Root deletes the log it is attached to. The bridge itself
    // is a function-local static and outlives everything, but the Log does not.
    if (Ogre::LogManager::getSingletonPtr())
        if (Ogre::Log* ogreLog =
                Ogre::LogManager::getSingleton().getDefaultLog())
            ogreLog->removeListener(&ogreLogBridge());
    delete mRoot; // last: tears down window, render system, resource managers
    mRoot = nullptr;
    mWindow = nullptr;
    mSceneMgr = nullptr;
    mCamera = nullptr;
    mViewport = nullptr;
}

} // namespace eng
