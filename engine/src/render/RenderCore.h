#pragma once
#include <cstdint>
#include <string>

#include <OgreTexture.h> // Ogre::TexturePtr member
#include <OgreRenderTargetListener.h> // base: draws imgui after the window RT

namespace Ogre {
class Root;
class RenderWindow;
class SceneManager;
class Camera;
class Viewport;
} // namespace Ogre

namespace eng {

// Visibility bit reserved for full-resolution world-space UI (text plaques).
// Objects tagged with ONLY this bit are culled from PSX/Stylized's low-res
// scene pass (`target mrt` masks it off) and drawn instead by the extra
// render_scene pass on target_output, at native window resolution. Ordinary
// movables keep Ogre's default 0xFFFFFFFF flags, so they pass both masks.
// MUST stay in sync with the visibility_mask in assets/compositors/psx.compositor.
inline constexpr uint32_t kFullResUiVisibilityFlag = 0x40000000u;
inline constexpr uint32_t kLowResSceneVisibilityMask = ~kFullResUiVisibilityFlag;

// Objects that exist ONLY inside the material-thumbnail render target. The
// thumbnail camera renders exactly this bit and the world cameras mask it off,
// so a preview sphere can sit at the origin of a live level without ever
// appearing in it.
inline constexpr uint32_t kThumbnailVisibilityFlag = 0x20000000u;
inline constexpr uint32_t kWorldVisibilityMask =
    ~(kFullResUiVisibilityFlag | kThumbnailVisibilityFlag);

// Internal: owns Ogre::Root and hides Ogre lifetime rules. Root is the first
// Ogre object created and the last destroyed. Also a RenderTargetListener: it
// paints the engine's own Dear ImGui frame onto the window after the scene (and
// PSX post chain) have drawn, but before the buffer swap.
class RenderCore : public Ogre::RenderTargetListener
{
public:
    ~RenderCore(); // calls shutdown(); safe if already shut down
    // sdlWindow is the SDL_Window* (as void*) the imgui SDL2 backend binds to
    // for input, cursors, and display size. The content packs must already be
    // mounted (Engine::init does it): the resource locations registered here
    // are exactly eng::assets::resourceDirs().
    bool init(uintptr_t nativeWindowHandle, void* sdlWindow, int width,
              int height, const std::string& title, bool vsync);
    // Brings up the PSX post chain (scene downscale + bloom + dither) if not
    // already active. Idempotent; the chain stays on once up.
    void enablePostChain();
    // Rebuilds the chain with RT sizes = window / pixelSize. Clamped 1..16.
    // Clears any absolute resolution set by setRenderResolution().
    void setPixelSize(int pixelSize);
    // Rebuilds the chain at an absolute RT size, independent of the window, for
    // the profiles that emulate a specific console framebuffer. Held across
    // window resizes. 0 in either axis reverts to the pixelSize divisor.
    void setRenderResolution(int width, int height);
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
    // The offscreen viewport paints its own background; the main viewport's
    // clear colour never reaches it, so an editor that wants a different
    // backdrop has to say so here.
    void setOffscreenBackground(float r, float g, float b);

    // --- material thumbnail target ---------------------------------------
    // A small square RTT with its own camera, for rendering one object (a
    // preview sphere) on its own. Separate from the editor viewport because it
    // has a different size, a different camera and a different subject; sharing
    // one target would mean re-rendering the whole scene per thumbnail.
    void enableThumbnailViewport(int size);
    void setThumbnailCameraPose(float px, float py, float pz, float qw, float qx,
                                float qy, float qz, float fovDeg);
    uint64_t thumbnailTextureId() const;
    bool thumbnailActive() const { return mThumbTex.get() != nullptr; }
    // Drive the editor RTT's dedicated camera (a free-fly editor eye, decoupled
    // from the game/window MainCamera). Quaternion is (w,x,y,z).
    void setEditorCameraPose(float px, float py, float pz,
                             float qw, float qx, float qy, float qz,
                             float fovDeg);

    // --- Dear ImGui (on-screen debug UI) ----------------------------------
    // The ImGui context + SDL2/OpenGL3 backends are created in init(). Call
    // beginImGuiFrame() once per frame BEFORE building any ImGui/ImGuizmo
    // windows; renderFrame() then calls ImGui::Render() and the window
    // RenderTargetListener blits the draw data over the final image. Frames
    // where beginImGuiFrame() is not called draw nothing (no windows built).
    void beginImGuiFrame(float dt);
    bool imguiReady() const { return mImGuiInit; }

    // Ogre::RenderTargetListener: paint imgui after the window's own render.
    void postRenderTargetUpdate(const Ogre::RenderTargetEvent& evt) override;

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

private:
    Ogre::Root* mRoot = nullptr;
    Ogre::RenderWindow* mWindow = nullptr;
    Ogre::SceneManager* mSceneMgr = nullptr;
    Ogre::Camera* mCamera = nullptr;
    Ogre::Viewport* mViewport = nullptr;
    void* mSdlWindow = nullptr;      // SDL_Window* for the imgui SDL2 backend
    bool mImGuiInit = false;         // context + backends up
    bool mImGuiFrameStarted = false; // beginImGuiFrame() called this frame
    bool mImGuiRendered = false;     // ImGui::Render() done -> listener may blit
    bool mChainAdded = false;   // compositor instance exists on the viewport
    bool mChainEnabled = false; // chain was ever requested (one-way; gates the
                                // setPixelSize re-add on cold start)
    int mPixelSize = 3;
    // > 0: absolute chain resolution, overriding mPixelSize (see
    // setRenderResolution). Both axes are set/cleared together.
    int mTargetW = 0, mTargetH = 0;
    // Patches the compositor definition's texture factors from mPixelSize or
    // mTargetW/H and rebuilds the window's chain instance.
    void applyChainSizes();

    // Editor offscreen RTT (scene + post baked into a texture for ImGui::Image).
    Ogre::TexturePtr mOffscreenTex;
    Ogre::TexturePtr mThumbTex;
    Ogre::Viewport* mThumbVp = nullptr;
    Ogre::Camera* mThumbCam = nullptr;
    Ogre::SceneNode* mThumbCamNode = nullptr;
    Ogre::Viewport* mOffscreenVp = nullptr;
    Ogre::Camera* mEditorCam = nullptr;    // dedicated free-fly eye for the RTT
    Ogre::SceneNode* mEditorCamNode = nullptr; // carries mEditorCam's transform
    int mOffW = 0, mOffH = 0;
};

} // namespace eng
