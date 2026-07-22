#pragma once
#include "ScenePanel.h"
#include <eng/MaterialPreview.h>
#include <memory>
namespace eng { class Renderer; class SceneView; }
namespace eng::ui {
struct Selection;
// Right-hand inspector: Add Entity + component groups for the selected node +
// the entity list (via ScenePanel::drawList). Draws into the CURRENT window.
class InspectorPanel {
public:
    void draw(Renderer& r, const SceneView& scene, Selection& sel);
private:
    ScenePanel mTree;
    std::unique_ptr<MaterialPreview> mPreview;
};
}
