#pragma once

#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace eng {

// Stable mixer routes. Master is the engine endpoint; every other value owns a
// miniaudio sound group feeding it. Keep this list small: authored content
// picks a route, while settings and snapshots tune the route rather than every
// clip.
enum class AudioBus : std::uint8_t {
    Master,
    Music,
    Ambience,
    Dialogue,
    Weapons,
    Sfx,
    Ui,
    Warnings,
    Count,
};

inline constexpr std::size_t kAudioBusCount =
    static_cast<std::size_t>(AudioBus::Count);

inline constexpr const char* audioBusName(AudioBus bus)
{
    switch (bus) {
    case AudioBus::Master:
        return "Master";
    case AudioBus::Music:
        return "Music";
    case AudioBus::Ambience:
        return "Ambience";
    case AudioBus::Dialogue:
        return "Dialogue";
    case AudioBus::Weapons:
        return "Weapons";
    case AudioBus::Sfx:
        return "SFX";
    case AudioBus::Ui:
        return "UI";
    case AudioBus::Warnings:
        return "Warnings";
    case AudioBus::Count:
        break;
    }
    return "Invalid";
}

inline float decibelsToLinear(float db)
{
    if (!std::isfinite(db) || db <= -80.0f)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

inline float linearToDecibels(float linear)
{
    if (!std::isfinite(linear) || linear <= 0.0001f)
        return -80.0f;
    return std::clamp(20.0f * std::log10(linear), -80.0f, 12.0f);
}

// How a stopped sound behaves. AllowFadeOut uses a short de-click fade before
// miniaudio stops the voice; Immediate is for teardown and hard state changes.
enum class StopMode { Immediate, AllowFadeOut };

enum class AudioPriority : std::uint8_t {
    Background = 16,
    Low = 48,
    Normal = 80,
    Important = 112,
    Critical = 144,
};

struct AudioListener {
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 velocity{0.0f};
};

// Per-voice tuning applied before playback starts. Gains are authored in dB;
// conversion to miniaudio's linear amplitude happens only at the backend edge.
struct PlaybackSettings {
    AudioBus bus = AudioBus::Sfx;
    float gainDb = 0.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool streaming = false;
    bool spatialized = false;
    bool listenerRelative = false;

    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float minDistance = 1.0f;
    float maxDistance = 45.0f;
    float rolloff = 1.0f;
    float dopplerFactor = 1.0f;

    float fadeInSeconds = 0.0f;
    // Zero starts immediately. Non-zero is an absolute frame on miniaudio's
    // output clock, allowing sample-aligned stems and beat-quantized segments.
    std::uint64_t startFrame = 0;
    AudioPriority priority = AudioPriority::Normal;
    bool stealable = true;
};

struct AudioStats {
    std::size_t activeVoices = 0;
    std::size_t voiceLimit = 0;
    std::array<std::size_t, kAudioBusCount> voicesByBus{};
    std::uint64_t voicesStarted = 0;
    std::uint64_t voicesStolen = 0;
    std::uint64_t voicesRejected = 0;
    bool nullBackend = false;
};

} // namespace eng
