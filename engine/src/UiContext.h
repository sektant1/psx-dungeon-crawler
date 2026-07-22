#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <string>

namespace eng { class RenderCore; }

namespace eng::ui {

// Reusable Dear ImGui + SDL plumbing, shared by the debug HUD and the editor.
// Wraps Ogre's ImGuiOverlay (NewFrame) and the SDL->imgui event feed.
class Context {
public:
    void init(RenderCore* core);
    bool onEvent(const SDL_Event& e); // true when imgui consumed the event
    void beginFrame(float dt);        // set DisplaySize + ImGuiOverlay::NewFrame() + push frametime
    void setHudPrompt(const std::string& text);
    void drawHudPrompt();             // bottom-centre borderless prompt (call each frame)
    float avgFrameMs() const;
    const std::array<float, 120>& frames() const { return mFrameMs; }
    int frameIndex() const { return mFrameMsIdx; }
    RenderCore* core() const { return mCore; }
private:
    RenderCore* mCore = nullptr;
    std::string mHudPrompt;
    std::array<float, 120> mFrameMs{};
    int mFrameMsIdx = 0;
};

} // namespace eng::ui
