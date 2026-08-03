#pragma once
#include <miniaudio.h>
#include <eng/SoundInstance.h>

#include <cstdint>

// Private to engine/src/audio — the ma_sound-owning definition of
// SoundInstance::Impl, shared between SoundInstance.cpp and Audio.cpp. Never
// included from a public eng/ header (keeps miniaudio out of the public API).
namespace eng {
struct SoundInstance::Impl {
    ma_sound sound{};
    AudioBus bus = AudioBus::Sfx;
    AudioPriority priority = AudioPriority::Normal;
    std::uint64_t serial = 0;
    bool stealable = true;
    bool paused = false;
    bool stopRequested = false;
};
} // namespace eng
