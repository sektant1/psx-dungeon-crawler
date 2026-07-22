#pragma once
#include <eng/Handles.h>
#include <memory>
namespace eng { class Renderer; }
namespace eng {
// Composes the editor's dock panels (Settings, Profiler, Inspector) around a
// central Scene viewport, over a full-window imgui DockSpace. The caller passes
// the RTT texture id each frame; EditorUi draws it as the Scene image.
class EditorUi {
public:
    explicit EditorUi(Renderer& r);
    ~EditorUi();
    void draw(uint64_t viewportTexId);
    NodeHandle selected() const;
    bool viewportHovered() const;
    struct Size { int w = 0, h = 0; };
    Size viewportSize() const; // last Scene-dock content size (for RTT resize)
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
}
