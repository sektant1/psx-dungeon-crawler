#pragma once
#include <eng/DebugUi.h>

#include <SDL2/SDL.h>

#include <glm/glm.hpp>

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
    float ditherDarkFade = 0.06f;

    // Pixel-art stylizer tunables (defaults match Stylize_FS in psx.program;
    // colours are raw sRGB, mixed post-encode like the Godot reference).
    bool stylizeShadows = true;
    bool stylizeHighlights = true;
    float shadowStrength = 0.4f;
    float highlightStrength = 0.1f;
    glm::vec3 shadowColor{0.0f};
    glm::vec3 highlightColor{1.0f};

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
