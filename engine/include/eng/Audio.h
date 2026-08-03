#pragma once
#include <eng/AudioTypes.h>
#include <eng/SoundInstance.h>
#include <eng/systems/System.h>
#include <memory>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace eng {

// Audio subsystem backed by miniaudio's high-level engine (device + mixing +
// decoded-file cache). Public header is backend-free (pimpl). Construct with
// nullBackend=true for headless/test use (no hardware device).
class Audio : public System {
public:
    explicit Audio(bool nullBackend = false);
    ~Audio() override;

    // Boot the device + engine; returns false on failure (e.g. no device).
    // Named startup() rather than reusing System::initialize() because that
    // returns void and this needs a success result for headless/test callers.
    bool startup();

    void initialize() override { startup(); } // System lifecycle hook
    void update(float dt) override;
    void terminate() override;

    // Play `path` with `settings`; returns a retained instance handle (also
    // tracked internally so it keeps playing even if the caller drops its Ptr).
    SoundInstance::Ptr play(const std::string& path,
                            const PlaybackSettings& settings = {});

    // Fire-and-forget; no handle. Returns false if the file could not start.
    bool playOneShot(const std::string& path,
                     const PlaybackSettings& settings = {});

    // Mixer controls. Base gain and duck gain are combined in dB, then smoothed
    // on update so settings, snapshots and dialogue do not click or pump.
    void setBusGainDb(AudioBus bus, float db);
    float busGainDb(AudioBus bus) const;
    void setBusMuted(AudioBus bus, bool muted);
    void setBusDuckDb(AudioBus bus, float db, float attackSeconds = 0.025f,
                      float releaseSeconds = 0.4f);
    void setBusVolume(AudioBus bus, float normalized);
    void setMasterVolume(float linear); // compatibility edge; prefer dB above

    void setListener(const AudioListener& listener);
    void setMaxVoices(std::size_t count);
    std::size_t maxVoices() const;
    std::uint64_t clockFrame() const;
    std::uint32_t sampleRate() const;

    std::size_t activeCount() const;
    std::size_t activeCount(AudioBus bus) const;
    AudioStats stats() const;
    bool ready() const { return mInitialized; }

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    std::vector<SoundInstance::Ptr> mInstances;
    bool mNullBackend;
    bool mInitialized = false;
};

} // namespace eng
