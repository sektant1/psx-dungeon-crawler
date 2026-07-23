#pragma once

namespace eng {

// How a stopped sound behaves (mirrors the SPEngine sample). AllowFadeOut is
// advisory for now — miniaudio stops immediately; fades are a future addition.
enum class StopMode { Immediate, AllowFadeOut };

// Per-playback tuning applied when a sound starts. Plain POD; no backend types
// so this header stays usable anywhere without pulling in miniaudio.
struct PlaybackSettings {
    float volume = 1.0f;   // linear gain, 1.0 = unity
    float pitch  = 1.0f;   // playback rate multiplier, 1.0 = original
    bool  loop   = false;
};

} // namespace eng
