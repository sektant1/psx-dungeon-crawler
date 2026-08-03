#pragma once
#include <string>

namespace eng {

// Persistence for the tool UI's *layout only*: where each debug panel is
// docked, which node it shares, its size, and whether it is collapsed. That is
// exactly what Dear ImGui's ini format stores -- no slider value, no render
// profile, no gameplay state ever goes in it, because none of that lives in an
// imgui window in the first place. Every tuning control in this engine writes
// straight through to the system it edits.
//
// It exists so an arrangement can be *shipped*. The default path is inside the
// engine's assets, so dragging the panels into the layout you want and
// committing the file is the whole workflow; a build that wants the shipped
// arrangement and nothing else simply never writes to it.
//
// --- turning it off ---------------------------------------------------------
//
// One switch, in this order of precedence:
//
//   1. Define ENG_UI_LAYOUT_PERSISTENCE=0 at build time. The engine then never
//      opens the file at all and DebugTools rebuilds its shipped layout every
//      run, which is the pre-existing behaviour and what a shipping build
//      wants.
//   2. RAVEN_UI_LAYOUT=0 (or "off"/"none") in the environment, per run.
//   3. RAVEN_UI_LAYOUT=<path> to point it somewhere else -- a scratch file while
//      experimenting, or a per-user path outside the source tree.
//
// Headless captures set (1) or (2): a restored layout would make a screenshot
// of the tool UI depend on what somebody dragged last session.
//
// It sits under render/ rather than debug/ because RenderCore owns the imgui
// context and has to call install() the moment it is created -- and RenderCore
// is a layer below the debug panels, so a header filed with them would be one
// it is not allowed to include.
namespace imgui_layout {

// False when persistence is compiled out or switched off for this run. Callers
// must treat it as the only gate -- path() is meaningless when this is false.
bool enabled();

// Absolute path of the ini, or empty when disabled. Stable across working
// directories: a dev tool that saved into the CWD would scatter a file wherever
// the binary happened to be launched from.
const std::string& path();

// True when a layout file already existed at startup and imgui restored from
// it. DebugTools uses this to decide whether to impose its shipped dock layout:
// building over a restored one would silently undo the arrangement this whole
// facility exists to keep.
bool restored();

// Called once by RenderCore right after the imgui context is created. Resolves
// the switches above, points io.IniFilename at the result, and records whether
// the file was already there. Safe to call when disabled: it leaves
// io.IniFilename null, which is imgui's "never persist" setting.
//
// The file is named after the running executable
// (`debug_layout_<game|psx_demo|scene_editor>.ini`) so the three apps keep
// separate arrangements. They share window *names* -- "Render", "Materials" --
// but not window sets, and one file would have them dragging each other's
// panels around. The window title is deliberately not the identity: it carries
// a build tag, and a layout that moved when the title did would be worse than
// none. An explicit RAVEN_UI_LAYOUT path overrides this and is taken literally,
// which is what makes "point all three at one file" still possible.
void install();

// Flush now rather than waiting out imgui's save timer. RenderCore calls it on
// shutdown, because up to io.IniSavingRate seconds of arranging would otherwise
// be lost every time the app closes -- which reads as "it does not save".
void save();

} // namespace imgui_layout
} // namespace eng
