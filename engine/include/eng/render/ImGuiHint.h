#pragma once

#include <string>
#include <vector>

namespace eng {

// Tool tooltips for the editor and the debug UI.
//
// Same justification as imguitheme: every tool app shares one engine imgui
// context, so hover help is engine furniture rather than per-app code. Before
// this, each site hand-rolled `if (ImGui::IsItemHovered()) ImGui::SetTooltip`
// with an inline literal, which is why the undo tooltip promised by
// Commands.h never appeared and why no hint text could be reviewed or
// translated in one place.
//
// Hint text is data: engine/assets/ui/hints.toml maps dotted ids to a title
// and a body. An id with no entry falls back to the literal passed at the call
// site, so migrating a site is a one-line change that cannot regress.
namespace imguihint {

struct Hint {
    std::string title;
    std::string body;
};

// Loads (or reloads) the hint table. Path is a filesystem path; call it once
// after imgui exists. Returns false and keeps the previous table on failure.
bool load(const std::string& tomlPath);

// Looks a hint up. Returns nullptr when the id is unknown.
const Hint* find(const std::string& id);

// Registers or overrides one hint at runtime, for panels that generate their
// own ids (kit pieces, materials).
void set(const std::string& id, Hint hint);

std::vector<std::string> ids();

// Shows a styled tooltip for the item just submitted, if it is hovered.
// `fallback` is used when `id` is not in the table; passing an empty id shows
// the fallback directly. Returns true when a tooltip was drawn.
bool hover(const std::string& id, const char* fallback = nullptr);

// The `(?)` marker plus its hover tooltip, as one call.
void marker(const std::string& id, const char* fallback = nullptr);

// Ad-hoc tooltip with the same styling but caller-owned text: for content that
// is inherently dynamic (a mesh path, a transform, an undo label).
void showText(const char* title, const char* body);

} // namespace imguihint
} // namespace eng
