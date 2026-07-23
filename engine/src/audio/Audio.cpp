#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <eng/Audio.h>
#include "AudioInternal.h"
#include <algorithm>

namespace eng {

struct Audio::Impl {
    ma_engine  engine;
    ma_context context;          // only used on the null backend
    bool       haveContext = false;
    bool       haveEngine  = false;
};

Audio::Audio(bool nullBackend)
    : System("Audio"), mImpl(std::make_unique<Impl>()), mNullBackend(nullBackend) {}

Audio::~Audio() { terminate(); }

bool Audio::startup() {
    if (mInitialized) return true;
    ma_engine_config cfg = ma_engine_config_init();
    if (mNullBackend) {
        ma_backend backends[] = { ma_backend_null };
        if (ma_context_init(backends, 1, nullptr, &mImpl->context) != MA_SUCCESS) return false;
        mImpl->haveContext = true;
        cfg.pContext = &mImpl->context;
    }
    if (ma_engine_init(&cfg, &mImpl->engine) != MA_SUCCESS) {
        if (mImpl->haveContext) { ma_context_uninit(&mImpl->context); mImpl->haveContext = false; }
        return false;
    }
    mImpl->haveEngine = true;
    mInitialized = true;
    return true;
}

static void applySettings(ma_sound* s, const PlaybackSettings& st) {
    ma_sound_set_volume(s, st.volume);
    ma_sound_set_pitch(s, st.pitch);
    ma_sound_set_looping(s, st.loop ? MA_TRUE : MA_FALSE);
}

SoundInstance::Ptr Audio::play(const std::string& path, const PlaybackSettings& settings) {
    if (!mInitialized) return nullptr;
    auto impl = std::make_unique<SoundInstance::Impl>();
    if (ma_sound_init_from_file(&mImpl->engine, path.c_str(),
            MA_SOUND_FLAG_DECODE, nullptr, nullptr, &impl->sound) != MA_SUCCESS)
        return nullptr;
    applySettings(&impl->sound, settings);
    if (ma_sound_start(&impl->sound) != MA_SUCCESS) { ma_sound_uninit(&impl->sound); return nullptr; }
    // Private ctor reachable here via friendship.
    SoundInstance::Ptr inst(new SoundInstance(std::move(impl)));
    mInstances.push_back(inst);
    return inst;
}

bool Audio::playOneShot(const std::string& path, const PlaybackSettings& settings) {
    if (!mInitialized) return false;
    // ma_engine_play_sound is fire-and-forget but exposes no per-voice settings;
    // for anything non-default, route through a tracked instance instead.
    if (settings.volume == 1.0f && settings.pitch == 1.0f && !settings.loop)
        return ma_engine_play_sound(&mImpl->engine, path.c_str(), nullptr) == MA_SUCCESS;
    return play(path, settings) != nullptr;
}

void Audio::setMasterVolume(float v) { if (mInitialized) ma_engine_set_volume(&mImpl->engine, v); }

void Audio::update(float /*dt*/) {
    // Drop instances that finished (not looping and no longer playing). Looping
    // and paused instances report !isPlaying too, so this also releases sounds a
    // caller stopped and dropped — intended (the retained handle keeps a live one
    // playing; a stopped one is done).
    mInstances.erase(std::remove_if(mInstances.begin(), mInstances.end(),
        [](const SoundInstance::Ptr& p) { return !p->isPlaying(); }), mInstances.end());
}

std::size_t Audio::activeCount() const {
    std::size_t n = 0;
    for (const auto& p : mInstances) if (p->isPlaying()) ++n;
    return n;
}

void Audio::terminate() {
    mInstances.clear();          // uninit every ma_sound before the engine it references
    if (mImpl && mImpl->haveEngine)  { ma_engine_uninit(&mImpl->engine);  mImpl->haveEngine = false; }
    if (mImpl && mImpl->haveContext) { ma_context_uninit(&mImpl->context); mImpl->haveContext = false; }
    mInitialized = false;
}

} // namespace eng
