#include <eng/SoundInstance.h>
#include "AudioInternal.h"

namespace eng {

SoundInstance::SoundInstance(std::unique_ptr<Impl> impl) : mImpl(std::move(impl)) {}

SoundInstance::~SoundInstance() {
    if (mImpl) ma_sound_uninit(&mImpl->sound);
}

void SoundInstance::setVolume(float v)  { if (mImpl) ma_sound_set_volume(&mImpl->sound, v); }
void SoundInstance::setPitch(float p)   { if (mImpl) ma_sound_set_pitch(&mImpl->sound, p); }
void SoundInstance::setLooping(bool l)  { if (mImpl) ma_sound_set_looping(&mImpl->sound, l ? MA_TRUE : MA_FALSE); }

// pause() halts but keeps the play cursor, so resume() continues in place.
void SoundInstance::pause()  { if (mImpl) ma_sound_stop(&mImpl->sound); }
void SoundInstance::resume() { if (mImpl) ma_sound_start(&mImpl->sound); }

// stop() halts and rewinds to the start (a subsequent start replays from 0).
void SoundInstance::stop(StopMode) {
    if (mImpl) { ma_sound_stop(&mImpl->sound); ma_sound_seek_to_pcm_frame(&mImpl->sound, 0); }
}

bool SoundInstance::isPlaying() const {
    return mImpl && ma_sound_is_playing(&mImpl->sound) == MA_TRUE;
}

} // namespace eng
