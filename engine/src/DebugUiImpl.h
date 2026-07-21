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
    float colDepth = 15.0f;
    float ditherBanding = 0.5f; // pattern amplitude 0..1, not just on/off
    float ditherDarkFade = 0.12f;

    // Pixel-art stylizer tunables (colours are raw sRGB, mixed post-encode
    // like the Godot reference). Initial values mirror the verdigris grade
    // the game applies at startup (main.cpp setMaterialParam calls), NOT the
    // neutral Stylize_FS defaults -- otherwise the first slider touch snaps
    // the shipped tint back to black/white.
    bool stylizeShadows = true;
    bool stylizeHighlights = true;
    float shadowStrength = 0.45f;
    float highlightStrength = 0.12f;
    glm::vec3 shadowColor{0.03f, 0.07f, 0.035f};
    glm::vec3 highlightColor{0.94f, 0.88f, 0.72f};
    float outlineThickness = 1.0f;   // edge tap offset in low-res pixels
    float shadowThreshold = 0.25f;   // depth-edge detector centre
    float highlightThreshold = 0.5f; // normal-edge detector centre
    float highlightDarkFade = 0.15f; // luma where highlights reach full
    // Ink outline (Boltgun-style hard contour).
    bool outlineEnabled = true;
    glm::vec3 outlineColor{0.0f};
    float outlineOpacity = 0.85f;
    float outlineDepthSens = 15.0f;
    float outlineNormalSens = 0.6f;
    float outlineSharpness = 0.85f;
    float outlineDistFade = 0.08f;
    float outlineDarkFade = 0.12f; // scene luma where ink reaches full

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
