#pragma once

#include <cstdint>

namespace ed {

// Stable ids after ### preserve ImGui state while visible panel names can stay
// specific to the author's task.
//
// The visible names are Godot's, on purpose. An author who has used one engine
// should not have to learn that this tree's "Hierarchy" is that tree's "Scene"
// and this tree's "Asset Browser" is that tree's "FileSystem"; the ids behind
// them stay what they always were, so no saved layout is invalidated by the
// rename.
namespace workspace_window {
// The centre. Two windows in one dock node, swapped by the main-screen
// switcher: the level, and the flat page. The material stage is a *mode* of the
// 3D one rather than a third window -- it is the same viewport looking at a
// different scene, and giving it its own panel would mean two viewport images
// alive at once for a renderer that has one offscreen target.
inline constexpr const char* kViewport3D = "3D###Viewport";
inline constexpr const char* kViewport2D = "2D###UI";

// The left column, split. Godot's arrangement, and the argument for it is
// height: the tree and the asset list are each read top-to-bottom, so tabbing
// them meant whichever one you were not using was wasting the whole rail.
inline constexpr const char* kSceneTree = "Scene###Outliner";
inline constexpr const char* kLayers = "Layers###Layers";
inline constexpr const char* kFileSystem = "FileSystem###Catalog";

// The right column. Godot puts Inspector / Node / History here; the middle one
// has no analogue in this engine, and what belongs in its place is the scene
// contract -- "what kind of scene is this, and what is it missing" -- which
// until now was buried below the issue list in a bottom tab.
inline constexpr const char* kInspector = "Inspector###Inspector";
inline constexpr const char* kContract = "Contract###Contract";
inline constexpr const char* kHistory = "History###History";
} // namespace workspace_window

// Pixel targets are converted to split ratios only when the default workspace
// is built. Side rails stay nearly fixed on ultrawide screens, leaving extra
// width to the scene instead of stretching property fields across the display.
struct WorkspacePlan {
    float leftPixels = 0.0f;
    float rightPixels = 0.0f;
    // The bottom panel is not a dock node (see EditorShell.h). This is only its
    // opening height, so the plan still owns every size the workspace has.
    float bottomPanelPixels = 0.0f;
    // The share of the left column given to the scene tree, the rest going to
    // the file system beneath it. A share rather than a pixel count because
    // both lists scroll and neither has a natural height.
    float sceneTreeFraction = 0.0f;
};

WorkspacePlan makeWorkspacePlan(float width, float height, float uiScale);

// Replaces the node and builds the canonical workspace. This is deliberately
// separate from EditorApp: panel topology is one policy, not incidental code in
// a nine-thousand-line frame callback.
void buildEditorWorkspace(std::uint32_t dockspaceId, float width, float height,
                          float uiScale);

} // namespace ed
