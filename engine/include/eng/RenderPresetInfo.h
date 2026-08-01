#pragma once
#include <vector>

namespace eng {

class Renderer;

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

// The look applied when nothing asks for anything else. "dungeon" is a real
// profile like any other, not an unprofiled fallback path.
inline constexpr int kDefaultRenderPreset = 7;

// A preset name from the table above -> its id; unknown -> -1.
int renderPresetFromName(const char* name);

// The inverse, for logs and UI labels. Never null: an id outside the table
// reports "unknown" rather than crashing a log line.
const char* renderPresetName(int id);

// The bloom a profile applies. The rest of a profile stays engine-private on
// purpose (see applyRenderPreset below), but bloom is *global* post that other
// panels mirror -- the portal tuner shows it because a portal authored above
// 1.0 is half an effect without it -- and a mirror seeded from a guess is a
// slider that jumps the frame the first time it is touched.
struct RenderPresetBloom {
    bool enabled = true;
    float threshold = 0.72f;
    float intensity = 0.72f;
};
RenderPresetBloom renderPresetBloom(int id);

// Reads `--render-preset <name>` off the command line. Returns 0 when the
// switch is absent, so a caller can fall through to the environment and then to
// kDefaultRenderPreset; an unknown name warns and also returns 0 rather than
// dropping the app into an undefined look.
int renderPresetFromArgs(int argc, const char* const* argv);

// Applies the preset with this id to the renderer. An id outside the table
// applies the built-in defaults rather than doing nothing, so a bad config
// value still lands the app in a defined look.
//
// The tuned field-by-field values behind an id stay engine-private on purpose:
// a game picks a profile, it does not hand-assemble one. Live per-field editing
// is the debug console's job (eng::DebugTools), not a game's.
void applyRenderPreset(Renderer& renderer, int id);

} // namespace eng
