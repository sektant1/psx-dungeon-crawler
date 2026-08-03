#include "audio/GameAudio.h"

#include <eng/Audio.h>
#include <eng/FileSystem.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace game;

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "GameAudioTests: " << message << '\n';
        std::exit(1);
    }
}

static bool writeTone(const std::string& path)
{
    const std::uint32_t sampleRate = 8000;
    const std::uint16_t channels = 1;
    const std::uint16_t bits = 16;
    const std::uint32_t frames = 1600;
    std::vector<std::int16_t> pcm(frames);
    for (std::uint32_t i = 0; i < frames; ++i)
        pcm[i] = static_cast<std::int16_t>(std::sin(i * 0.1) * 2500.0);
    const std::uint32_t dataBytes = frames * channels * (bits / 8);
    const std::uint32_t byteRate = sampleRate * channels * (bits / 8);
    const std::uint16_t blockAlign = channels * (bits / 8);
    const std::uint32_t riffSize = 36 + dataBytes;
    std::ofstream output(path, std::ios::binary);
    if (!output)
        return false;
    const auto u32 = [&](std::uint32_t value) {
        output.write(reinterpret_cast<const char*>(&value), 4);
    };
    const auto u16 = [&](std::uint16_t value) {
        output.write(reinterpret_cast<const char*>(&value), 2);
    };
    output.write("RIFF", 4);
    u32(riffSize);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    u32(16);
    u16(1);
    u16(channels);
    u32(sampleRate);
    u32(byteRate);
    u16(blockAlign);
    u16(bits);
    output.write("data", 4);
    u32(dataBytes);
    output.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
    return output.good();
}

int main()
{
    AudioCatalog authored;
    require(parseAudioCatalog(std::string(PROJECT_SOURCE_DIR) +
                                  "/assets/config/audio.toml",
                              authored),
            "shipped audio catalog parses");
    require(authored.cue(audioCueId("weapon.vesper.fire")) != nullptr,
            "shipped weapon cue exists");
    require(authored.music.stems.size() == 6, "shipped adaptive stems parse");

    MusicMixState calm;
    require(targetMusicIntensity(calm) == 0.0f, "calm intensity is zero");
    calm.threat = 0.5f;
    calm.playerHealth = 0.25f;
    require(targetMusicIntensity(calm) > 0.5f,
            "low health raises existing combat pressure");
    calm.boss = true;
    require(targetMusicIntensity(calm) == 1.0f, "boss intensity is full");
    calm = {};
    calm.threat = std::numeric_limits<float>::quiet_NaN();
    require(targetMusicIntensity(calm) == 0.0f,
            "non-finite intensity input falls back safely");
    require(quantizeAudioFrame(100, 48000, 120.0f, 4) == 96000,
            "transition quantizes to next bar");
    require(quantizeAudioFrame(96000, 48000, 120.0f, 4) == 192000,
            "exact boundary advances to following bar");
    require(quantizeAudioFrame(
                123, 48000, std::numeric_limits<float>::quiet_NaN(), 4) == 123,
            "invalid tempo leaves schedule immediate");

    AudioCatalog invalid;
    require(!parseAudioCatalogText(
                "[[cue]]\nid='bad'\nfiles=['bad.wav']\nbus='typo'\n", invalid),
            "invalid route rejects catalog with no usable cues");

    const std::string dir = "/tmp/game_audio_test";
    eng::FileSystem::directoryCreate(dir);
    const std::string wave = dir + "/tone.wav";
    const std::string config = dir + "/audio.toml";
    require(writeTone(wave), "test wave created");
    {
        std::ofstream output(config);
        output << "[mixer]\nmax_voices = 8\nmaster_db = -4\n"
               << "[[cue]]\nid = \"loop\"\nfiles = [\"" << wave
               << "\"]\nbus = \"ambience\"\nloop = true\nspatial = true\n"
               << "max_distance = 10\nmax_instances = 1\nlimit = \"reject\"\n"
               << "[[cue]]\nid = \"cooldown\"\nfiles = [\"" << wave
               << "\"]\nspatial = false\ncooldown_seconds = 0.2\n"
               << "max_instances = 4\n";
    }

    eng::Audio backend(true);
    require(backend.startup(), "null miniaudio backend starts");
    GameAudioSystem audio;
    require(audio.load(backend, config, 7), "runtime catalog loads");
    audio.setListener({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 0.016f);

    AudioEmission nearby;
    nearby.position = {0.0f, 0.0f, -2.0f};
    auto loop = audio.emit("loop", nearby);
    require(loop != nullptr, "positional loop emits");
    require(audio.emit("loop", nearby) == nullptr,
            "per-cue concurrency rejects second loop");

    AudioEmission distant;
    distant.position = {0.0f, 0.0f, -20.0f};
    loop->stop(eng::StopMode::Immediate);
    audio.update(0.016f);
    backend.update(0.016f);
    require(audio.emit("loop", distant) == nullptr,
            "inaudible positional cue is culled before voice creation");

    auto first = audio.emit("cooldown");
    require(first != nullptr, "cooldown cue emits once");
    require(audio.emit("cooldown") == nullptr, "cooldown suppresses repeat");
    audio.update(0.25f);
    require(audio.emit("cooldown") != nullptr,
            "cooldown expires on audio wall clock");

    AudioEmitter emitter(audio, audioCueId("loop"));
    require(emitter.play({0.0f, 0.0f, -1.0f}), "emitter component starts");
    emitter.setTransform({1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f});
    require(emitter.playing(), "emitter retains active voice");
    emitter.stop(eng::StopMode::Immediate);
    require(!emitter.playing(), "emitter stops safely");

    const GameAudioStats stats = audio.stats();
    require(stats.emitted >= 4, "emission telemetry counts starts");
    require(stats.concurrencyRejected == 1, "concurrency telemetry counts");
    require(stats.cooldownRejected == 1, "cooldown telemetry counts");
    require(stats.distanceCulled == 1, "distance telemetry counts");

    audio.stopAll(eng::StopMode::Immediate);
    backend.terminate();
    std::cout << "GameAudioTests OK\n";
    return 0;
}
