#pragma once
#include <eng/DebugUi.h>

#include "UiContext.h"
#include "panels/RenderTuningPanel.h"

#include <SDL2/SDL.h>

#include <string>
#include <utility>
#include <vector>

namespace eng {

class RenderCore;
class Renderer;

struct DebugUi::Impl {
    RenderCore* core = nullptr;
    Renderer* renderer = nullptr;
    bool visible = false;
    bool mainWindowVisible = true;

    eng::ui::Context ctx; // shared imgui + SDL plumbing

    std::vector<std::pair<std::string, std::function<void()>>> panels;
    std::vector<std::function<void()>> windows;

    eng::ui::RenderTuningPanel tuning;

    void init(RenderCore* c, Renderer* r);

    // Feeds one SDL event to ImGui. Returns true when ImGui consumed it
    // (event must then not reach eng::Input). Always returns false while
    // the panel is hidden.
    bool onEvent(const SDL_Event& e);

    // Per frame, before renderOneFrame: ImGuiOverlay::NewFrame() + widgets.
    void buildFrame(float dt);
};

} // namespace eng
