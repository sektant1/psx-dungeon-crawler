#pragma once

#include "CommandStack.h" // Command
#include "Gizmo.h"        // GizmoMode
#include "Picker.h"       // Ray
#include "Selection.h"

#include <eng/ecs/Components.h> // eng::ecs::Transform

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <utility>
#include <vector>

namespace editor {

// Tunables the gizmo reads each drag frame (may change mid-drag via the toolbar).
struct GizmoConfig {
    GizmoMode mode = GizmoMode::Translate;
    bool uniformScale = true; // scale drag affects all axes together
    float snapStep = 0.0f;    // 0 = free; units for Move/Scale, degrees for Rotate
    float gridSize = 0.0f;    // 0 = off; world-grid snap for Move (overrides snapStep)
};

// The doodad-manipulation state machine. Hit-tests an axis at the selection
// centroid, drags the whole selection about that pivot per the active mode
// (translate / rotate / scale), and hands back one undoable Command on release.
// All eight mid-drag fields are private -- the interface is begin/drag/release +
// dragging(). The geometry is pure (Picker/Gizmo helpers), so the drag math is
// exercisable through a scripted ray sequence with no renderer or ImGui frame.
class GizmoTool {
public:
    // Try to grab an axis under `ray` from the selection `centroid`. Starts a
    // drag and returns true if an axis was hit and the selection has a valid
    // primary; returns false otherwise (the caller should fall back to picking).
    bool begin(entt::registry& reg, const Selection& sel, const Ray& ray,
               glm::vec3 centroid, const GizmoConfig& cfg);

    bool dragging() const { return mDragging; }

    // Continue the drag: map the axis delta over every selected entity's
    // pre-drag transform and write it back to the registry.
    void drag(entt::registry& reg, const Ray& ray, const GizmoConfig& cfg);

    // End the drag. Restores each entity to its pre-drag transform and returns a
    // composite Command that re-applies the dragged finals (so undo/redo is one
    // step for the whole selection). Returns a null Command (`.apply == nullptr`)
    // if nothing was grabbed; the caller runs it on the stack only when set.
    Command release(entt::registry& reg);

private:
    bool mDragging = false;
    int mAxis = 0; // 0=x, 1=y, 2=z
    eng::ecs::Transform mPreDrag; // primary's transform at grab (translate ref)
    std::vector<std::pair<entt::entity, eng::ecs::Transform>> mPre; // all selected
    glm::vec3 mStartHit{0.0f};
    glm::vec3 mCentroid{0.0f}; // pivot for rotate/scale
    glm::vec3 mStartVec{0.0f}; // rotate reference vector (pivot -> pointer)
    float mStartT = 0.0f;      // axis param at grab (scale reference)
};

} // namespace editor
