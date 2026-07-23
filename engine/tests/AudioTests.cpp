#include <eng/AudioTypes.h>
#include <eng/SoundResource.h>
#include <eng/Audio.h>
#include <eng/SoundInstance.h>
#include <eng/FileSystem.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"AudioTests: "<<m<<'\n'; std::exit(1);} }

// Minimal valid 16-bit PCM mono WAV so miniaudio has something real to decode.
static bool writeSineWav(const std::string& path){
    const uint32_t sampleRate = 8000;
    const uint16_t channels = 1, bits = 16;
    const uint32_t frames = 400;                 // ~50ms
    std::vector<int16_t> pcm(frames);
    for (uint32_t i = 0; i < frames; ++i)
        pcm[i] = static_cast<int16_t>(std::sin(i * 0.1) * 3000.0);

    const uint32_t dataBytes = frames * channels * (bits/8);
    const uint32_t byteRate = sampleRate * channels * (bits/8);
    const uint16_t blockAlign = channels * (bits/8);
    const uint32_t riffSize = 36 + dataBytes;

    std::ofstream o(path, std::ios::binary);
    if (!o) return false;
    auto u32 = [&](uint32_t v){ o.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v){ o.write(reinterpret_cast<const char*>(&v), 2); };
    o.write("RIFF", 4); u32(riffSize); o.write("WAVE", 4);
    o.write("fmt ", 4); u32(16); u16(1); u16(channels);
    u32(sampleRate); u32(byteRate); u16(blockAlign); u16(bits);
    o.write("data", 4); u32(dataBytes);
    o.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
    return o.good();
}

int main(){
    // --- PlaybackSettings defaults ---
    PlaybackSettings s;
    require(s.volume==1.0f && s.pitch==1.0f && !s.loop, "settings defaults");

    // --- SoundResource records path; load() succeeds iff file exists ---
    const std::string dir = "/tmp/eng_audio_test";
    FileSystem::directoryCreate(dir);
    const std::string wav = dir + "/blip.wav";
    FileSystem::fileWrite(wav, "RIFFxxxxWAVE"); // stub; existence is all load() checks
    SoundResource sr("blip", wav);
    require(!sr.loaded(), "not loaded before load()");
    require(sr.load() && sr.loaded(), "load succeeds for existing file");
    require(sr.name()=="blip" && sr.path()==wav, "name/path retained");
    SoundResource missing("nope", dir + "/absent.wav");
    require(!missing.load(), "load fails for missing file");

    // --- Audio system on the NULL backend (headless, no device) ---
    Audio audio(/*nullBackend=*/true);
    require(audio.name()=="Audio", "system name");
    require(audio.startup(), "engine inits on null backend");

    const std::string realWav = dir + "/tone.wav";
    require(writeSineWav(realWav), "wrote test wav");

    PlaybackSettings loud; loud.volume = 0.5f; loud.loop = true;
    auto inst = audio.play(realWav, loud);
    require(inst != nullptr, "play returns an instance");
    require(inst->isPlaying(), "instance is playing");
    inst->setVolume(0.25f);
    inst->setPitch(1.5f);
    require(audio.activeCount()==1, "one active instance");
    inst->stop();
    require(!inst->isPlaying(), "stopped instance not playing");

    // one-shot: fire and forget, no handle retained
    require(audio.playOneShot(realWav, PlaybackSettings{}), "one-shot plays");

    audio.setMasterVolume(0.8f);
    audio.update(0.016f);      // prunes finished
    audio.terminate();

    // Retained handle must survive terminate() without touching a dead engine.
    require(!inst->isPlaying(), "finalized instance is inert after terminate");
    inst->setVolume(0.5f);     // no-op, must not crash
    inst.reset();              // dtor must not double-uninit

    std::cout << "AudioTests OK\n";
    return 0;
}
