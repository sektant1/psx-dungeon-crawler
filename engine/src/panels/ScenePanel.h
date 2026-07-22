#pragma once
#include <eng/Handles.h>
namespace eng { class SceneView; }
namespace eng::ui {
struct Selection;
// Renders the entity tree as ImGui rows into the CURRENT window (no Begin/End),
// so it can live inside the Inspector dock. Writes clicks to sel.
class ScenePanel {
public:
    void drawList(const SceneView& scene, Selection& sel);
private:
    void drawNode(const SceneView& scene, NodeHandle n, Selection& sel);
};
}
