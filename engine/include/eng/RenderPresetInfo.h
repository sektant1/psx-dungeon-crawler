#pragma once
#include <vector>

namespace eng {

// A shipped render preset: the name a config file or command line uses, and
// the id every preset call takes.
//
// The table this exposes *is* the numbering. Before it existed, a UI listing
// the presets carried its own array of names and relied on its index matching
// the engine's internal ids -- a contract nothing checked and a comment had to
// keep alive. Ask for the list, apply the id you were handed.
struct RenderPresetInfo {
    const char* name;
    int id;
};

// In id order, so index i is id i+1. Never empty.
const std::vector<RenderPresetInfo>& renderPresets();

} // namespace eng
