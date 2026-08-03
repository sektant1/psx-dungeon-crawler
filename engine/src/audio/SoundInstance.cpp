#include <eng/SoundInstance.h>
#include "AudioInternal.h"

#include <algorithm>
#include <cmath>

namespace eng {

SoundInstance::SoundInstance(std::unique_ptr<Impl> impl)
    : mImpl(std::move(impl))
{
}

SoundInstance::~SoundInstance()
{
    if (mImpl)
        ma_sound_uninit(&mImpl->sound);
}

void SoundInstance::finalize()
{
    if (mImpl) {
        ma_sound_uninit(&mImpl->sound);
        mImpl.reset();
    }
}

void SoundInstance::setVolume(float v)
{
    if (mImpl)
        ma_sound_set_volume(&mImpl->sound, std::max(0.0f, v));
}
void SoundInstance::setGainDb(float db)
{
    if (mImpl)
        ma_sound_set_volume(&mImpl->sound, decibelsToLinear(db));
}
void SoundInstance::setPitch(float p)
{
    if (mImpl)
        ma_sound_set_pitch(&mImpl->sound, std::max(0.01f, p));
}
void SoundInstance::setLooping(bool l)
{
    if (mImpl)
        ma_sound_set_looping(&mImpl->sound, l ? MA_TRUE : MA_FALSE);
}

void SoundInstance::setPosition(glm::vec3 p)
{
    if (mImpl)
        ma_sound_set_position(&mImpl->sound, p.x, p.y, p.z);
}

void SoundInstance::setVelocity(glm::vec3 v)
{
    if (mImpl)
        ma_sound_set_velocity(&mImpl->sound, v.x, v.y, v.z);
}

void SoundInstance::fadeToGainDb(float db, float seconds)
{
    if (!mImpl)
        return;
    const auto ms = static_cast<ma_uint64>(std::max(0.0f, seconds) * 1000.0f);
    ma_sound_set_fade_in_milliseconds(&mImpl->sound, -1.0f,
                                      decibelsToLinear(db), ms);
}

void SoundInstance::scheduleStop(std::uint64_t absoluteFrame, float fadeSeconds)
{
    if (!mImpl)
        return;
    const ma_uint32 rate =
        ma_engine_get_sample_rate(ma_sound_get_engine(&mImpl->sound));
    const ma_uint64 fadeFrames = static_cast<ma_uint64>(
        std::max(0.0f, fadeSeconds) * static_cast<float>(rate));
    ma_sound_set_stop_time_with_fade_in_pcm_frames(&mImpl->sound, absoluteFrame,
                                                   fadeFrames);
    mImpl->stopRequested = true;
}

// pause() halts but keeps the play cursor, so resume() continues in place.
void SoundInstance::pause()
{
    if (!mImpl || mImpl->stopRequested)
        return;
    ma_sound_stop(&mImpl->sound);
    mImpl->paused = true;
}
void SoundInstance::resume()
{
    if (!mImpl || mImpl->stopRequested)
        return;
    mImpl->paused = false;
    ma_sound_start(&mImpl->sound);
}

// stop() halts and rewinds to the start (a subsequent start replays from 0).
void SoundInstance::stop(StopMode mode)
{
    if (!mImpl)
        return;
    mImpl->paused = false;
    mImpl->stopRequested = true;
    if (mode == StopMode::AllowFadeOut) {
        ma_sound_stop_with_fade_in_milliseconds(&mImpl->sound, 100);
    }
    else {
        ma_sound_stop(&mImpl->sound);
        ma_sound_seek_to_pcm_frame(&mImpl->sound, 0);
    }
}

bool SoundInstance::isPlaying() const
{
    return mImpl && ma_sound_is_playing(&mImpl->sound) == MA_TRUE;
}

bool SoundInstance::paused() const
{
    return mImpl && mImpl->paused;
}

bool SoundInstance::finished() const
{
    if (!mImpl)
        return true;
    if (ma_sound_at_end(&mImpl->sound) == MA_TRUE)
        return true;
    return mImpl->stopRequested && !isPlaying();
}

float SoundInstance::cursorSeconds() const
{
    float seconds = 0.0f;
    if (mImpl)
        ma_sound_get_cursor_in_seconds(&mImpl->sound, &seconds);
    return seconds;
}

float SoundInstance::lengthSeconds() const
{
    float seconds = 0.0f;
    if (mImpl)
        ma_sound_get_length_in_seconds(&mImpl->sound, &seconds);
    return seconds;
}

} // namespace eng
