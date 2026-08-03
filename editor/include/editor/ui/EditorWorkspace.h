#pragma once

#include <cstdint>

namespace ed {

// Stable ids after ### preserve ImGui state while visible panel names can stay
// specific to the author's task.
namespace workspace_window {
inline constexpr const char* kCommandBar = "Command Bar###Toolbar";
inline constexpr const char* kSceneView = "Scene View###Viewport";
inline constexpr const char* kHudPreview = "HUD Preview###UI";
inline constexpr const char* kAssetBrowser = "Asset Browser###Catalog";
inline constexpr const char* kHierarchy = "Hierarchy###Outliner";
inline constexpr const char* kInspector = "Inspector";
inline constexpr const char* kProblems = "Problems###Issues";
inline constexpr const char* kConsole = "Console";
} // namespace workspace_window

// Pixel targets are converted to split ratios only when the default workspace
// is built. Side rails stay nearly fixed on ultrawide screens, leaving extra
// width to the scene instead of stretching property fields across the display.
struct WorkspacePlan {
    float leftPixels = 0.0f;
    float rightPixels = 0.0f;
    float commandBarPixels = 0.0f;
    float diagnosticsPixels = 0.0f;
};

WorkspacePlan makeWorkspacePlan(float width, float height, float uiScale);

// Replaces the node and builds the canonical workspace. This is deliberately
// separate from EditorApp: panel topology is one policy, not incidental code in
// a six-thousand-line frame callback.
void buildEditorWorkspace(std::uint32_t dockspaceId, float width, float height,
                          float uiScale);

} // namespace ed
