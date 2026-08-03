#pragma once
#include <eng/AudioTypes.h>
#include <cstdint>
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
    void setGainDb(float db);
    void setPitch(float p);
    void setLooping(bool loop);
    void setPosition(glm::vec3 position);
    void setVelocity(glm::vec3 velocity);
    void fadeToGainDb(float db, float seconds);
    void scheduleStop(std::uint64_t absoluteFrame, float fadeSeconds = 0.0f);
    void pause();
    void resume();
    void stop(StopMode mode = StopMode::Immediate);
    bool isPlaying() const;
    bool paused() const;
    bool finished() const;
    float cursorSeconds() const;
    float lengthSeconds() const;

    using Ptr = std::shared_ptr<SoundInstance>;

private:
    friend class Audio;
    struct Impl;
    explicit SoundInstance(std::unique_ptr<Impl> impl);

    // Release the backend voice while the owning engine is still alive. Called
    // by Audio::terminate() so instances a caller still holds don't later
    // uninit against a destroyed engine. After this, every method is a no-op.
    void finalize();

    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
