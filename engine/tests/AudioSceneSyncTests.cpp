#include <eng/Audio.h>
#include <eng/ecs/World.h>

#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

using namespace eng;
using namespace eng::ecs;

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "AudioSceneSyncTests: " << message << '\n';
        std::exit(1);
    }
}

static bool writeWav(const std::filesystem::path& path)
{
    constexpr std::uint32_t sampleRate = 8000;
    constexpr std::uint32_t frames = 800;
    std::vector<std::int16_t> pcm(frames);
    for (std::uint32_t index = 0; index < frames; ++index)
        pcm[index] = static_cast<std::int16_t>(std::sin(index * 0.1) * 3000.0);

    const std::uint32_t dataBytes = frames * sizeof(std::int16_t);
    const std::uint32_t riffBytes = 36 + dataBytes;
    std::ofstream output(path, std::ios::binary);
    const auto u32 = [&](std::uint32_t value) {
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    const auto u16 = [&](std::uint16_t value) {
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    output.write("RIFF", 4);
    u32(riffBytes);
    output.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(sampleRate);
    u32(sampleRate * sizeof(std::int16_t));
    u16(sizeof(std::int16_t));
    u16(16);
    output.write("data", 4);
    u32(dataBytes);
    output.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
    return output.good();
}

int main()
{
    const std::filesystem::path directory = "/tmp/eng_audio_scene_sync";
    std::filesystem::create_directories(directory);
    const std::filesystem::path clip = directory / "loop.wav";
    require(writeWav(clip), "test clip is written");

    Audio audio(/*nullBackend=*/true);
    require(audio.startup(), "null audio backend starts");

    World world;
    world.attachAudio(audio, /*drivesListener=*/true);
    const entt::entity entity = world.create("empty_audio_node");
    AudioEmitter emitter;
    emitter.source = clip.string();
    emitter.loop = true;
    emitter.offset = {1.0f, 0.0f, 0.0f};
    world.registry().emplace<AudioEmitter>(entity, emitter);
    world.registry().emplace<eng::ecs::AudioListener>(
        entity, eng::ecs::AudioListener{10, true});

    world.sync();
    require(audio.activeCount() == 1, "empty entity starts one authored voice");
    require(!world.registry().all_of<NodeRef>(entity),
            "audio does not allocate a renderer node");
    const std::uint64_t started = audio.stats().voicesStarted;

    Transform moved;
    moved.position = {5.0f, 2.0f, -3.0f};
    world.setLocalTransform(entity, moved);
    world.sync();
    require(audio.stats().voicesStarted == started,
            "moving an emitter updates its voice without restarting it");

    world.registry().get<AudioEmitter>(entity).gainDb = -12.0f;
    world.sync();
    require(audio.stats().voicesStarted == started + 1,
            "tweaking playback settings restarts exactly once");

    world.registry().get<AudioEmitter>(entity).playing = false;
    world.sync();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    audio.update(0.12f);
    require(audio.activeCount() == 0, "disabling emitter stops its voice");

    world.registry().get<AudioEmitter>(entity).playing = true;
    world.sync();
    require(audio.activeCount() == 1, "re-enabling emitter starts it again");

    world.registry().remove<AudioEmitter>(entity);
    world.sync();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    audio.update(0.12f);
    require(audio.activeCount() == 0, "removing component releases its voice");

    world.detachAll();
    audio.terminate();
    std::cout << "AudioSceneSyncTests OK\n";
    return 0;
}
