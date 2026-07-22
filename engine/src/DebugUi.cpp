#include "DebugUiImpl.h"

#include <eng/Renderer.h>

#include "RenderCore.h"

#include <OgreImGuiOverlay.h>

#include <imgui.h>

#include <string>

namespace eng {

DebugUi::DebugUi() : mImpl(new Impl) {}
DebugUi::~DebugUi() = default;

void DebugUi::Impl::init(RenderCore* c, Renderer* r)
{
    core = c;
    renderer = r;
    ctx.init(c);
    tuning.init(r);
}

void DebugUi::addPanel(const std::string& name, std::function<void()> draw)
{
    mImpl->panels.emplace_back(name, std::move(draw));
}

void DebugUi::addWindow(std::function<void()> draw)
{
    mImpl->windows.emplace_back(std::move(draw));
}

bool DebugUi::visible() const { return mImpl->visible; }
void DebugUi::setVisible(bool v) { mImpl->visible = v; }
void DebugUi::setMainWindowVisible(bool visible)
{
    mImpl->mainWindowVisible = visible;
}

void DebugUi::setHudPrompt(const std::string& text) { mImpl->ctx.setHudPrompt(text); }

bool DebugUi::Impl::onEvent(const SDL_Event& e)
{
    if (!visible)
        return false;
    return ctx.onEvent(e);
}

void DebugUi::Impl::buildFrame(float dt)
{
    ctx.beginFrame(dt);
    ctx.drawHudPrompt();

    if (!visible)
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (mainWindowVisible) {
        const ImVec2 panelSize(std::min(380.0f, io.DisplaySize.x - 20.0f),
                               std::min(540.0f, io.DisplaySize.y - 20.0f));
        ImGui::SetNextWindowSize(panelSize, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(300, 260),
                                            ImVec2(io.DisplaySize.x - 20.0f,
                                                   io.DisplaySize.y - 20.0f));
        ImGui::Begin("Engine Diagnostics");
        if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen))
            tuning.drawStats(ctx);
        if (ImGui::CollapsingHeader("PSX Shaders"))
            tuning.drawShaders();
        if (ImGui::CollapsingHeader("Pixel Art", ImGuiTreeNodeFlags_DefaultOpen))
            tuning.drawPixelArt();
        if (ImGui::CollapsingHeader("Camera"))
            tuning.drawCamera();
        for (auto& [name, fn] : panels)
            if (ImGui::CollapsingHeader(name.c_str()))
                fn();
        if (ImGui::Button("Copy all as TOML"))
            tuning.copyToml();
        ImGui::End();
    }
    for (auto& draw : windows)
        draw();
}

} // namespace eng
