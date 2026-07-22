#pragma once
#include <eng/Handles.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
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

    // Scene picker: a Godot-style dropdown in the Scene toolbar to open one of
    // the discovered scene files. `label` is what shows in the combo; the
    // callback receives the chosen entry (empty path = the built-in procedural
    // scene). The app owns discovery + the actual (re)load.
    struct SceneFile { std::string label; std::string path; };
    void setSceneFiles(std::vector<SceneFile> files, int active);
    using LoadSceneFn = std::function<void(const SceneFile&)>;
    void setLoadSceneCallback(LoadSceneFn cb);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
}
