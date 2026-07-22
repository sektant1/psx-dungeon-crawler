#pragma once
#include <eng/Config.h>
#include <eng/DebugUi.h>
#include <eng/Input.h>
#include <eng/Renderer.h>
#include <eng/System.h>

#include <memory>
#include <string>
#include <vector>

namespace eng {

// Owns lifetime and ordering: SDL window -> Ogre Root -> (frames) ->
// Ogre Root down -> SDL window down. Also owns the frame clock and the
// PSX_SCREENSHOT verification hook (render 90 frames by default, save PNG,
// close). PSX_SCREENSHOT_FRAME overrides the capture frame for animation tests.
class Engine
{
public:
    Engine();
    ~Engine();

    // Loads TOML config (window.title/width/height + [bindings]), creates
    // the window, brings up the renderer with engine + app asset roots.
    bool init(const std::string& configPath, const std::string& appAssetDir);

    float tick(); // pump events, update input; returns dt clamped to 0.1 s
    bool shouldClose() const { return mClose; }
    void requestClose() { mClose = true; }
    void renderFrame(float dt); // may set shouldClose() (screenshot hook)
    void presentLoadingFrame(const std::string& title,
                             const std::string& label,
                             float progress01);
    void shutdown();

    Renderer& renderer() { return mRenderer; }
    Input& input() { return mInput; }
    Config& config() { return mConfig; }
    DebugUi& debugUi() { return mDebugUi; }

    // System registry (SPEngine-style). Additive: the existing tick()/
    // renderFrame() loop is unchanged; a game opts in by registering systems
    // and calling updateSystems(dt) from its loop.
    System* registerSystem(System::StrongPtr sys);
    void updateSystems(float dt);

    template <typename T> T* getSystem() {
        for (auto& s : mSystems)
            if (auto* p = dynamic_cast<T*>(s.get())) return p;
        return nullptr;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    Config mConfig;
    Input mInput;
    Renderer mRenderer;
    DebugUi mDebugUi;
    bool mClose = false;
    std::vector<System::StrongPtr> mSystems;
};

} // namespace eng
