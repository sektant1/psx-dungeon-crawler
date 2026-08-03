#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <eng/Audio.h>
#include <eng/Log.h>

#include "AudioInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace eng {
namespace {

constexpr std::size_t busIndex(AudioBus bus)
{
    return static_cast<std::size_t>(bus);
}

constexpr std::size_t groupIndex(AudioBus bus)
{
    return busIndex(bus) - 1;
}

struct BusState {
    float baseDb = 0.0f;
    float duckTargetDb = 0.0f;
    float duckCurrentDb = 0.0f;
    float attackSeconds = 0.025f;
    float releaseSeconds = 0.4f;
    bool muted = false;
};

float approach(float current, float target, float seconds, float dt)
{
    if (seconds <= 0.0f || dt <= 0.0f)
        return target;
    const float alpha = 1.0f - std::exp(-dt / seconds);
    return current + (target - current) * alpha;
}

} // namespace

struct Audio::Impl {
    ma_engine engine{};
    ma_context context{};
    std::array<ma_sound_group, kAudioBusCount - 1> groups{};
    std::array<bool, kAudioBusCount - 1> haveGroup{};
    std::array<BusState, kAudioBusCount> buses{};
    bool haveContext = false;
    bool haveEngine = false;
    bool usingNullBackend = false;
    std::size_t voiceLimit = 96;
    std::uint64_t serial = 0;
    std::uint64_t voicesStarted = 0;
    std::uint64_t voicesStolen = 0;
    std::uint64_t voicesRejected = 0;
};

Audio::Audio(bool nullBackend)
    : System("Audio"), mImpl(std::make_unique<Impl>()),
      mNullBackend(nullBackend)
{
}

Audio::~Audio()
{
    terminate();
}

bool Audio::startup()
{
    if (mInitialized)
        return true;

    ma_engine_config config = ma_engine_config_init();
    config.gainSmoothTimeInMilliseconds = 20;
    config.defaultVolumeSmoothTimeInPCMFrames = 960; // 20 ms at 48 kHz

    const auto initialize = [&](bool nullBackend) {
        if (nullBackend) {
            ma_backend backends[] = {ma_backend_null};
            if (ma_context_init(backends, 1, nullptr, &mImpl->context) !=
                MA_SUCCESS)
                return false;
            mImpl->haveContext = true;
            config.pContext = &mImpl->context;
        }
        if (ma_engine_init(&config, &mImpl->engine) != MA_SUCCESS) {
            if (mImpl->haveContext) {
                ma_context_uninit(&mImpl->context);
                mImpl->haveContext = false;
            }
            config.pContext = nullptr;
            return false;
        }
        mImpl->haveEngine = true;
        mImpl->usingNullBackend = nullBackend;
        return true;
    };

    // A missing device must not make a headless capture or remote playtest
    // fail. Fall back to miniaudio's clocked null backend; playback remains a
    // safe no-op and all cue/concurrency/music logic continues to run.
    if (!initialize(mNullBackend)) {
        if (mNullBackend || !initialize(true))
            return false;
        log::warn("Audio: output device unavailable; using null backend");
    }

    for (std::size_t i = 0; i < mImpl->groups.size(); ++i) {
        if (ma_sound_group_init(&mImpl->engine, MA_SOUND_FLAG_NO_SPATIALIZATION,
                                nullptr, &mImpl->groups[i]) != MA_SUCCESS) {
            terminate();
            return false;
        }
        mImpl->haveGroup[i] = true;
    }

    mInitialized = true;
    update(0.0f);
    return true;
}

static void applySettings(ma_sound* sound, const PlaybackSettings& settings)
{
    ma_sound_set_volume(sound, decibelsToLinear(settings.gainDb));
    ma_sound_set_pitch(sound, std::max(0.01f, settings.pitch));
    ma_sound_set_looping(sound, settings.loop ? MA_TRUE : MA_FALSE);
    if (settings.spatialized) {
        ma_sound_set_position(sound, settings.position.x, settings.position.y,
                              settings.position.z);
        ma_sound_set_velocity(sound, settings.velocity.x, settings.velocity.y,
                              settings.velocity.z);
        ma_sound_set_positioning(sound, settings.listenerRelative
                                            ? ma_positioning_relative
                                            : ma_positioning_absolute);
        ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(sound, std::max(0.01f, settings.minDistance));
        ma_sound_set_max_distance(
            sound, std::max(settings.minDistance, settings.maxDistance));
        ma_sound_set_rolloff(sound, std::max(0.0f, settings.rolloff));
        ma_sound_set_doppler_factor(sound,
                                    std::max(0.0f, settings.dopplerFactor));
    }
    if (settings.fadeInSeconds > 0.0f) {
        if (settings.startFrame != 0) {
            const ma_uint64 fadeFrames = static_cast<ma_uint64>(
                settings.fadeInSeconds *
                float(ma_engine_get_sample_rate(ma_sound_get_engine(sound))));
            ma_sound_set_fade_start_in_pcm_frames(sound, 0.0f, 1.0f, fadeFrames,
                                                  settings.startFrame);
        }
        else {
            const auto milliseconds =
                static_cast<ma_uint64>(settings.fadeInSeconds * 1000.0f);
            ma_sound_set_fade_in_milliseconds(sound, 0.0f, 1.0f, milliseconds);
        }
    }
    if (settings.startFrame != 0)
        ma_sound_set_start_time_in_pcm_frames(sound, settings.startFrame);
}

SoundInstance::Ptr Audio::play(const std::string& path,
                               const PlaybackSettings& settings)
{
    if (!mInitialized || path.empty() || settings.bus == AudioBus::Count)
        return nullptr;

    mInstances.erase(std::remove_if(mInstances.begin(), mInstances.end(),
                                    [](const SoundInstance::Ptr& voice) {
                                        return !voice || voice->finished();
                                    }),
                     mInstances.end());

    while (mInstances.size() >= mImpl->voiceLimit) {
        auto candidate = mInstances.end();
        for (auto it = mInstances.begin(); it != mInstances.end(); ++it) {
            const auto& voice = *it;
            if (!voice || !voice->mImpl || !voice->mImpl->stealable ||
                static_cast<unsigned>(voice->mImpl->priority) >
                    static_cast<unsigned>(settings.priority))
                continue;
            if (candidate == mInstances.end() ||
                static_cast<unsigned>(voice->mImpl->priority) <
                    static_cast<unsigned>((*candidate)->mImpl->priority) ||
                (voice->mImpl->priority == (*candidate)->mImpl->priority &&
                 voice->mImpl->serial < (*candidate)->mImpl->serial))
                candidate = it;
        }
        if (candidate == mInstances.end()) {
            ++mImpl->voicesRejected;
            return nullptr;
        }
        (*candidate)->stop(StopMode::Immediate);
        mInstances.erase(candidate);
        ++mImpl->voicesStolen;
    }

    auto voiceImpl = std::make_unique<SoundInstance::Impl>();
    ma_uint32 flags =
        settings.streaming ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (!settings.spatialized)
        flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    ma_sound_group* group = settings.bus == AudioBus::Master
                                ? nullptr
                                : &mImpl->groups[groupIndex(settings.bus)];
    if (ma_sound_init_from_file(&mImpl->engine, path.c_str(), flags, group,
                                nullptr, &voiceImpl->sound) != MA_SUCCESS) {
        ++mImpl->voicesRejected;
        return nullptr;
    }

    voiceImpl->bus = settings.bus;
    voiceImpl->priority = settings.priority;
    voiceImpl->serial = ++mImpl->serial;
    voiceImpl->stealable = settings.stealable;
    applySettings(&voiceImpl->sound, settings);
    if (ma_sound_start(&voiceImpl->sound) != MA_SUCCESS) {
        ma_sound_uninit(&voiceImpl->sound);
        return nullptr;
    }

    SoundInstance::Ptr voice(new SoundInstance(std::move(voiceImpl)));
    mInstances.push_back(voice);
    ++mImpl->voicesStarted;
    return voice;
}

bool Audio::playOneShot(const std::string& path,
                        const PlaybackSettings& settings)
{
    return play(path, settings) != nullptr;
}

void Audio::setBusGainDb(AudioBus bus, float db)
{
    if (bus == AudioBus::Count)
        return;
    mImpl->buses[busIndex(bus)].baseDb =
        std::clamp(std::isfinite(db) ? db : -80.0f, -80.0f, 12.0f);
}

float Audio::busGainDb(AudioBus bus) const
{
    return bus == AudioBus::Count ? -80.0f : mImpl->buses[busIndex(bus)].baseDb;
}

void Audio::setBusMuted(AudioBus bus, bool muted)
{
    if (bus != AudioBus::Count)
        mImpl->buses[busIndex(bus)].muted = muted;
}

void Audio::setBusDuckDb(AudioBus bus, float db, float attackSeconds,
                         float releaseSeconds)
{
    if (bus == AudioBus::Count)
        return;
    BusState& state = mImpl->buses[busIndex(bus)];
    state.duckTargetDb = std::clamp(std::min(0.0f, db), -80.0f, 0.0f);
    state.attackSeconds = std::max(0.0f, attackSeconds);
    state.releaseSeconds = std::max(0.0f, releaseSeconds);
}

void Audio::setBusVolume(AudioBus bus, float normalized)
{
    setBusGainDb(bus, linearToDecibels(std::clamp(normalized, 0.0f, 1.0f)));
}

void Audio::setMasterVolume(float linear)
{
    setBusVolume(AudioBus::Master, linear);
}

void Audio::setListener(const AudioListener& listener)
{
    if (!mInitialized)
        return;
    ma_engine_listener_set_position(&mImpl->engine, 0, listener.position.x,
                                    listener.position.y, listener.position.z);
    ma_engine_listener_set_direction(&mImpl->engine, 0, listener.forward.x,
                                     listener.forward.y, listener.forward.z);
    ma_engine_listener_set_world_up(&mImpl->engine, 0, listener.up.x,
                                    listener.up.y, listener.up.z);
    ma_engine_listener_set_velocity(&mImpl->engine, 0, listener.velocity.x,
                                    listener.velocity.y, listener.velocity.z);
}

void Audio::setMaxVoices(std::size_t count)
{
    mImpl->voiceLimit = std::clamp<std::size_t>(count, 8, 512);
}

std::size_t Audio::maxVoices() const
{
    return mImpl->voiceLimit;
}

std::uint64_t Audio::clockFrame() const
{
    return mInitialized ? ma_engine_get_time_in_pcm_frames(&mImpl->engine) : 0;
}

std::uint32_t Audio::sampleRate() const
{
    return mInitialized ? ma_engine_get_sample_rate(&mImpl->engine) : 48000;
}

void Audio::update(float dt)
{
    if (!mInitialized)
        return;
    if (!std::isfinite(dt) || dt < 0.0f)
        dt = 0.0f;
    mInstances.erase(std::remove_if(mInstances.begin(), mInstances.end(),
                                    [](const SoundInstance::Ptr& voice) {
                                        return !voice || voice->finished();
                                    }),
                     mInstances.end());

    for (std::size_t i = 0; i < kAudioBusCount; ++i) {
        BusState& state = mImpl->buses[i];
        const float seconds = state.duckTargetDb < state.duckCurrentDb
                                  ? state.attackSeconds
                                  : state.releaseSeconds;
        state.duckCurrentDb =
            approach(state.duckCurrentDb, state.duckTargetDb, seconds, dt);
        const float effectiveDb =
            state.muted ? -80.0f : state.baseDb + state.duckCurrentDb;
        const AudioBus bus = static_cast<AudioBus>(i);
        if (bus == AudioBus::Master)
            ma_engine_set_gain_db(&mImpl->engine, effectiveDb);
        else
            ma_sound_group_set_volume(&mImpl->groups[groupIndex(bus)],
                                      decibelsToLinear(effectiveDb));
    }
}

std::size_t Audio::activeCount() const
{
    return static_cast<std::size_t>(
        std::count_if(mInstances.begin(), mInstances.end(),
                      [](const SoundInstance::Ptr& voice) {
                          return voice && !voice->finished();
                      }));
}

std::size_t Audio::activeCount(AudioBus bus) const
{
    return static_cast<std::size_t>(
        std::count_if(mInstances.begin(), mInstances.end(),
                      [bus](const SoundInstance::Ptr& voice) {
                          return voice && voice->mImpl && !voice->finished() &&
                                 voice->mImpl->bus == bus;
                      }));
}

AudioStats Audio::stats() const
{
    AudioStats result;
    result.activeVoices = activeCount();
    result.voiceLimit = mImpl->voiceLimit;
    result.voicesStarted = mImpl->voicesStarted;
    result.voicesStolen = mImpl->voicesStolen;
    result.voicesRejected = mImpl->voicesRejected;
    result.nullBackend = mImpl->usingNullBackend;
    result.voicesByBus[busIndex(AudioBus::Master)] = result.activeVoices;
    for (std::size_t i = 1; i < kAudioBusCount; ++i)
        result.voicesByBus[i] = activeCount(static_cast<AudioBus>(i));
    return result;
}

void Audio::terminate()
{
    for (auto& voice : mInstances)
        if (voice)
            voice->finalize();
    mInstances.clear();
    if (mImpl) {
        for (std::size_t i = mImpl->groups.size(); i-- > 0;) {
            if (mImpl->haveGroup[i]) {
                ma_sound_group_uninit(&mImpl->groups[i]);
                mImpl->haveGroup[i] = false;
            }
        }
        if (mImpl->haveEngine) {
            ma_engine_uninit(&mImpl->engine);
            mImpl->haveEngine = false;
        }
        if (mImpl->haveContext) {
            ma_context_uninit(&mImpl->context);
            mImpl->haveContext = false;
        }
    }
    mInitialized = false;
}

} // namespace eng
