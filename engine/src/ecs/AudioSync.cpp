#include <eng/ecs/AudioSync.h>

#include <eng/Audio.h>
#include <eng/assets/AssetRoot.h>

#include <algorithm>
#include <filesystem>
#include <iterator>

namespace eng::ecs {
namespace {

bool sameSettings(const AudioEmitter& a, const AudioEmitter& b)
{
    return a.source == b.source && a.offset == b.offset && a.bus == b.bus &&
           a.gainDb == b.gainDb && a.pitch == b.pitch &&
           a.minDistance == b.minDistance && a.maxDistance == b.maxDistance &&
           a.rolloff == b.rolloff && a.dopplerFactor == b.dopplerFactor &&
           a.priority == b.priority && a.loop == b.loop &&
           a.streaming == b.streaming && a.spatialized == b.spatialized &&
           a.stealable == b.stealable;
}

std::string resolveSource(const std::string& authored)
{
    if (authored.empty())
        return {};
    std::error_code error;
    if (std::filesystem::is_regular_file(authored, error))
        return authored;
    const std::filesystem::path resolved = assets::resolve(authored);
    return resolved.empty() ? std::string{} : resolved.string();
}

AudioBus validBus(int value)
{
    if (value < 0 || value >= static_cast<int>(AudioBus::Count))
        return AudioBus::Sfx;
    return static_cast<AudioBus>(value);
}

AudioPriority validPriority(int value)
{
    if (value <= static_cast<int>(AudioPriority::Background))
        return AudioPriority::Background;
    if (value <= static_cast<int>(AudioPriority::Low))
        return AudioPriority::Low;
    if (value <= static_cast<int>(AudioPriority::Normal))
        return AudioPriority::Normal;
    if (value <= static_cast<int>(AudioPriority::Important))
        return AudioPriority::Important;
    return AudioPriority::Critical;
}

glm::vec3 emitterPosition(const WorldTransform& world,
                          const AudioEmitter& emitter)
{
    const Transform transform = decompose(world.matrix);
    return transform.position +
           transform.rotation * (transform.scale * emitter.offset);
}

} // namespace

void World::attachAudio(Audio& audio, bool drivesListener)
{
    mAudio = std::make_unique<AudioSync>(*this, audio, drivesListener);
}

AudioSync::AudioSync(World& world, Audio& audio, bool drivesListener)
    : mWorld(world), mAudio(audio), mDrivesListener(drivesListener)
{
}

AudioSync::~AudioSync()
{
    clear();
}

void AudioSync::clear()
{
    for (Tracked& tracked : mTracked)
        if (tracked.voice)
            tracked.voice->stop(StopMode::Immediate);
    mTracked.clear();
}

void AudioSync::sync()
{
    auto& registry = mWorld.registry();

    for (entt::entity entity :
         registry.view<AudioEmitter, WorldTransform>()) {
        const AudioEmitter& emitter = registry.get<AudioEmitter>(entity);
        auto found = std::find_if(mTracked.begin(), mTracked.end(),
                                  [entity](const Tracked& tracked) {
                                      return tracked.entity == entity;
                                  });
        if (found == mTracked.end()) {
            Tracked tracked;
            tracked.entity = entity;
            tracked.authored = emitter;
            mTracked.push_back(std::move(tracked));
            found = std::prev(mTracked.end());
        }

        Tracked& tracked = *found;
        if (!sameSettings(tracked.authored, emitter)) {
            if (tracked.voice)
                tracked.voice->stop(StopMode::AllowFadeOut);
            tracked.voice.reset();
            tracked.attempted = false;
        }
        tracked.authored = emitter;

        if (!emitter.playing || emitter.source.empty()) {
            if (tracked.voice)
                tracked.voice->stop(StopMode::AllowFadeOut);
            tracked.voice.reset();
            tracked.attempted = false;
            continue;
        }

        const glm::vec3 position =
            emitterPosition(registry.get<WorldTransform>(entity), emitter);
        if (!tracked.attempted) {
            tracked.attempted = true;
            PlaybackSettings settings;
            settings.bus = validBus(emitter.bus);
            settings.gainDb = emitter.gainDb;
            settings.pitch = std::max(emitter.pitch, 0.01f);
            settings.loop = emitter.loop;
            settings.streaming = emitter.streaming;
            settings.spatialized = emitter.spatialized;
            settings.position = position;
            settings.minDistance = std::max(emitter.minDistance, 0.0f);
            settings.maxDistance =
                std::max(emitter.maxDistance, settings.minDistance + 0.01f);
            settings.rolloff = std::max(emitter.rolloff, 0.0f);
            settings.dopplerFactor = std::max(emitter.dopplerFactor, 0.0f);
            settings.priority = validPriority(emitter.priority);
            settings.stealable = emitter.stealable;
            const std::string source = resolveSource(emitter.source);
            if (!source.empty())
                tracked.voice = mAudio.play(source, settings);
        }
        if (tracked.voice && !tracked.voice->finished())
            tracked.voice->setPosition(position);
    }

    mTracked.erase(
        std::remove_if(mTracked.begin(), mTracked.end(),
                       [&](Tracked& tracked) {
                           const bool gone = !registry.valid(tracked.entity) ||
                                             !registry.all_of<AudioEmitter,
                                                              WorldTransform>(
                                                 tracked.entity);
                           if (gone && tracked.voice)
                               tracked.voice->stop(StopMode::AllowFadeOut);
                           return gone;
                       }),
        mTracked.end());

    syncListener();
}

void AudioSync::syncListener()
{
    if (!mDrivesListener)
        return;
    auto& registry = mWorld.registry();
    entt::entity chosen = entt::null;
    const AudioListener* selected = nullptr;
    for (entt::entity entity :
         registry.view<AudioListener, WorldTransform>()) {
        const AudioListener& candidate = registry.get<AudioListener>(entity);
        if (!candidate.active)
            continue;
        if (!selected || candidate.priority > selected->priority) {
            chosen = entity;
            selected = &candidate;
        }
    }
    if (chosen == entt::null)
        return;

    const Transform transform =
        decompose(registry.get<WorldTransform>(chosen).matrix);
    eng::AudioListener listener;
    listener.position = transform.position;
    listener.forward =
        transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    listener.up = transform.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
    mAudio.setListener(listener);
}

} // namespace eng::ecs
