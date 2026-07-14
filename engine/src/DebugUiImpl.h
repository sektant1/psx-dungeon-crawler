#pragma once
#include <eng/DebugUi.h>

#include <SDL2/SDL.h>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace eng {

class RenderCore;
class Renderer;

struct DebugUi::Impl {
    RenderCore* core = nullptr;
    Renderer* renderer = nullptr;
    bool visible = false;

    std::vector<std::pair<std::string, std::function<void()>>> panels;

    // Stats panel: frametime ring buffer (ms).
    std::array<float, 120> frameMs{};
    int frameMsIdx = 0;

    // PSX shader tunables (UI-side cache; initial values match the
    // defaults in engine/assets/programs/psx.program + psx.material).
    float precisionMultiplier = 1.0f;
    float colDepth = 15.0f;
    bool ditherBanding = true;

    void init(RenderCore* c, Renderer* r) { core = c; renderer = r; }

    // Feeds one SDL event to ImGui. Returns true when ImGui consumed it
    // (event must then not reach eng::Input). Always returns false while
    // the panel is hidden.
    bool onEvent(const SDL_Event& e);

    // Per frame, before renderOneFrame: ImGuiOverlay::NewFrame() + widgets.
    void buildFrame(float dt);

    // Widget bodies (DebugUi.cpp).
    void drawStats();
    void drawShaders();
    void drawCamera();
    void copyToml();
};

} // namespace eng
