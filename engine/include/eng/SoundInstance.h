#pragma once
#include <eng/AudioTypes.h>
#include <memory>

namespace eng {

class Audio;

// A handle to one playing sound. Created by Audio::play(); owns its underlying
// backend voice and stops+releases it on destruction. Backend-free (pimpl) so
// this header never pulls in miniaudio. Not copyable.
class SoundInstance {
public:
    ~SoundInstance();
    SoundInstance(const SoundInstance&) = delete;
    SoundInstance& operator=(const SoundInstance&) = delete;

    void setVolume(float v);
    void setPitch(float p);
    void setLooping(bool loop);
    void pause();
    void resume();
    void stop(StopMode mode = StopMode::Immediate);
    bool isPlaying() const;

    using Ptr = std::shared_ptr<SoundInstance>;

private:
    friend class Audio;
    struct Impl;
    explicit SoundInstance(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
