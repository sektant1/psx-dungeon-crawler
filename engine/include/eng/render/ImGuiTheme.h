#pragma once

#include <functional>
#include <string>
#include <vector>

namespace eng {

// Named Dear ImGui themes, registered once and applied by id.
//
// Every tool app (game DebugUI, scene_editor, psx_demo) shares one engine
// imgui context, so the theme belongs to the engine rather than to any app.
// Applying a theme mutates the live ImGuiStyle, so it is safe to call between
// frames at any point after RenderCore initialised imgui.
namespace imguitheme {

// Ids of every registered theme, in registration order. "dark"/"light"/
// "classic" are the imgui built-ins; "raven_editor" is the engine default.
std::vector<std::string> ids();

// Applies the theme to the current imgui context. Returns false (leaving the
// style untouched) when the id is unknown.
bool apply(const std::string& id);

// Id applied by the last successful apply() call.
const std::string& current();

// Registers a theme. `fn` mutates the live ImGuiStyle; it is stored and re-run
// on every apply(), so hot-swapping stays cheap. Re-registering an existing id
// replaces it.
void registerTheme(const std::string& id, std::function<void()> fn);

} // namespace imguitheme
} // namespace eng
