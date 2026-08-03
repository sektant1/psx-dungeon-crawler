#pragma once

#include <eng/SoundInstance.h>
#include <eng/ecs/World.h>

#include <entt/entt.hpp>

#include <vector>

namespace eng {
class Audio;
}

namespace eng::ecs {

// Reconciles authored audio components with miniaudio voices. Audio remains a
// view of World state: adding/removing a component starts/stops its voice, and
// no renderer node is required for an emitter on an otherwise empty entity.
class AudioSync final : public WorldReconciler {
public:
    AudioSync(World& world, Audio& audio, bool drivesListener);
    ~AudioSync() override;

    AudioSync(const AudioSync&) = delete;
    AudioSync& operator=(const AudioSync&) = delete;

    void sync() override;
    void clear() override;

private:
    struct Tracked {
        entt::entity entity{entt::null};
        AudioEmitter authored;
        SoundInstance::Ptr voice;
        bool attempted = false;
    };

    void syncListener();

    World& mWorld;
    Audio& mAudio;
    std::vector<Tracked> mTracked;
    bool mDrivesListener = true;
};

} // namespace eng::ecs
