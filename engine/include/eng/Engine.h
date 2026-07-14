#pragma once
#include <eng/Config.h>
#include <eng/DebugUi.h>
#include <eng/Input.h>
#include <eng/Renderer.h>

#include <memory>
#include <string>

namespace eng {

// Owns lifetime and ordering: SDL window -> Ogre Root -> (frames) ->
// Ogre Root down -> SDL window down. Also owns the frame clock and the
// PSX_SCREENSHOT verification hook (render 90 frames, save PNG, close).
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
    void shutdown();

    Renderer& renderer() { return mRenderer; }
    Input& input() { return mInput; }
    Config& config() { return mConfig; }
    DebugUi& debugUi() { return mDebugUi; }

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    Config mConfig;
    Input mInput;
    Renderer mRenderer;
    DebugUi mDebugUi;
    bool mClose = false;
};

} // namespace eng
