#pragma once
#include <eng/DebugUi.h>

#include <SDL2/SDL.h>

#include <glm/glm.hpp>

#include <array>
#include <string>
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
    std::vector<std::function<void()>> windows;
    std::string hudPrompt; // drawn every frame while non-empty

    // Stats panel: frametime ring buffer (ms).
    std::array<float, 120> frameMs{};
    int frameMsIdx = 0;

    // PSX shader tunables (UI-side cache; initial values match the
    // defaults in engine/assets/programs/psx.program + psx.material).
    float precisionMultiplier = 1.0f;
    // Wireframe debug view (Wire_FS defaults).
    glm::vec3 wireColor{0.55f, 0.8f, 1.0f};
    float wireDepthFade = 0.0f;
    // Line thickness in screen pixels, faked via the pixelation RT (GL core
    // profile forbids glLineWidth > 1): render at 1/n res, nearest upscale.
    int wireThickness = 1;
    float colDepth = 15.0f;
    float ditherBanding = 0.5f; // pattern amplitude 0..1, not just on/off
    float ditherDarkFade = 0.12f;

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
    void drawPixelArt();
    void drawCamera();
    void copyToml();
};

} // namespace eng
