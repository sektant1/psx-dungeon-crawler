#pragma once
#include <functional>
#include <memory>
#include <string>

namespace eng {

// In-game debug panel (Dear ImGui). Hidden by default; F1 toggles it and
// releases/restores the mouse grab. While open, UI-captured input is not
// forwarded to eng::Input, so game code needs no capture checks.
//
// Panel callbacks may include <imgui.h> (Ogre's bundled copy is on the
// include path). Ogre headers stay engine-internal.
class DebugUi
{
public:
    // Adds a collapsible section to the Debug window, after the built-in
    // panels, in registration order. `draw` runs every frame the panel is
    // open and the header is expanded.
    void addPanel(const std::string& name, std::function<void()> draw);

    bool visible() const;
    void setVisible(bool v);

private:
    friend class Engine; // constructs, feeds events, builds frames
    DebugUi();
    ~DebugUi();
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
