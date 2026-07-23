#pragma once
#include <eng/AudioTypes.h>
#include <eng/SoundInstance.h>
#include <eng/System.h>
#include <memory>
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

    void initialize() override { startup(); }   // System lifecycle hook
    void update(float dt) override;             // prune finished instances
    void terminate() override;

    // Play `path` with `settings`; returns a retained instance handle (also
    // tracked internally so it keeps playing even if the caller drops its Ptr).
    SoundInstance::Ptr play(const std::string& path, const PlaybackSettings& settings = {});

    // Fire-and-forget; no handle. Returns false if the file could not start.
    bool playOneShot(const std::string& path, const PlaybackSettings& settings = {});

    void setMasterVolume(float v);
    std::size_t activeCount() const;   // retained, still-playing instances

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    std::vector<SoundInstance::Ptr> mInstances;
    bool mNullBackend;
    bool mInitialized = false;
};

} // namespace eng
