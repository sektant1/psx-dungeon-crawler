#pragma once

#include <eng/Audio.h>
#include <eng/StringId.h>

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eng {
class Config;
}

namespace game {

inline eng::StringId audioCueId(std::string_view name)
{
    return eng::StringId(eng::hashString(name));
}

enum class VoiceLimitPolicy { Reject, StealOldest };

struct AudioCueDefinition {
    eng::StringId id;
    std::string name;
    std::vector<std::string> samples;
    eng::AudioBus bus = eng::AudioBus::Sfx;
    float gainDb = 0.0f;
    float gainVariationDb = 0.0f;
    float pitchMin = 1.0f;
    float pitchMax = 1.0f;
    bool spatialized = true;
    bool streaming = false;
    bool looping = false;
    float minDistance = 1.0f;
    float maxDistance = 45.0f;
    float rolloff = 1.0f;
    float dopplerFactor = 1.0f;
    std::size_t maxInstances = 8;
    float cooldownSeconds = 0.0f;
    eng::AudioPriority priority = eng::AudioPriority::Normal;
    VoiceLimitPolicy limitPolicy = VoiceLimitPolicy::StealOldest;
    float duckMusicDb = 0.0f;
};

struct MusicStemDefinition {
    eng::StringId section;
    eng::StringId cue;
    float startsAt = 0.0f;
    float fullAt = 1.0f;
    float maxGainDb = 0.0f;
};

struct MusicDefinition {
    float bpm = 80.0f;
    int beatsPerBar = 4;
    int transitionBars = 1;
    float transitionSeconds = 0.35f;
    float intensityRiseSeconds = 0.8f;
    float intensityFallSeconds = 3.5f;
    eng::StringId defaultSection;
    eng::StringId combatStinger;
    eng::StringId bossStinger;
    std::vector<MusicStemDefinition> stems;
};

struct AudioCatalog {
    std::unordered_map<eng::StringId, AudioCueDefinition> cues;
    std::array<float, eng::kAudioBusCount> busGainDb{};
    std::size_t maxVoices = 96;
    float dialogueDuckDb = -9.0f;
    float duckAttackSeconds = 0.025f;
    float duckReleaseSeconds = 0.45f;
    MusicDefinition music;

    const AudioCueDefinition* cue(eng::StringId id) const;
};

bool parseAudioCatalog(const std::string& tomlPath, AudioCatalog& out);
bool parseAudioCatalogText(std::string_view source, AudioCatalog& out);

enum class SpatialOverride { CueDefault, Force2D, Force3D };

struct AudioEmission {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float gainDb = 0.0f;
    float pitch = 1.0f;
    SpatialOverride spatial = SpatialOverride::CueDefault;
    float fadeInSeconds = 0.0f;
    std::uint64_t startFrame = 0;
};

struct MusicMixState {
    float threat = 0.0f;
    float playerHealth = 1.0f;
    bool boss = false;
    bool extracting = false;
    bool carryingSignificantObject = false;
};

struct PlayerFoleyState {
    glm::vec3 feet{0.0f};
    float horizontalSpeed = 0.0f;
    bool grounded = false;
    bool sprinting = false;
    // Which cue each beat plays. Empty keeps the conventional ids, which is
    // what a caller with no actor table gets.
    //
    // The split is deliberate: *when* a footstep happens is foley timing and
    // belongs here (stride distance, sprint, the teleport guard), while *what*
    // it sounds like is the player's actor sound table and belongs to whoever
    // owns that. Hardcoding the ids here made the player the one actor whose
    // sounds could not be authored.
    std::string footstepCue;
    std::string landCue;
};

struct GameAudioStats {
    std::uint64_t emitted = 0;
    std::uint64_t cooldownRejected = 0;
    std::uint64_t concurrencyRejected = 0;
    std::uint64_t distanceCulled = 0;
    std::uint64_t unavailableRejected = 0;
    float musicIntensity = 0.0f;
    int musicTier = 0;
    eng::AudioStats backend;
};

float targetMusicIntensity(const MusicMixState& state);
std::uint64_t quantizeAudioFrame(std::uint64_t now, std::uint32_t sampleRate,
                                 float bpm, int beatsPerBar, int bars = 1);

// Game policy over eng::Audio: cue lookup, variation, concurrency, foley,
// dialogue ducking and adaptive music. It owns no device and can be rebuilt on
// content reload without restarting miniaudio.
class GameAudioSystem {
public:
    GameAudioSystem();
    ~GameAudioSystem();
    GameAudioSystem(const GameAudioSystem&) = delete;
    GameAudioSystem& operator=(const GameAudioSystem&) = delete;

    bool load(eng::Audio& audio, const std::string& catalogPath,
              std::uint32_t randomSeed = 0xA17D10u);
    void applyUserMix(const eng::Config& config);
    void stopAll(eng::StopMode mode = eng::StopMode::AllowFadeOut);

    eng::SoundInstance::Ptr emit(eng::StringId cue,
                                 const AudioEmission& emission = {});
    eng::SoundInstance::Ptr emit(std::string_view cue,
                                 const AudioEmission& emission = {})
    {
        return emit(audioCueId(cue), emission);
    }

    void setListener(glm::vec3 position, glm::vec3 forward, float realDt);
    void updatePlayerFoley(const PlayerFoleyState& state);
    void update(float realDt, const MusicMixState& music = {});
    void requestMusicSection(eng::StringId section);
    void requestMusicSection(std::string_view section)
    {
        requestMusicSection(audioCueId(section));
    }

    const AudioCatalog& catalog() const;
    const AudioCueDefinition* cue(eng::StringId id) const;
    GameAudioStats stats() const;
    bool loaded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

// Reusable component for a positional one-shot or loop attached to any game
// object. Entity/scene ownership stays outside; this only owns its current
// voice.
class AudioEmitter {
public:
    AudioEmitter() = default;
    AudioEmitter(GameAudioSystem& system, eng::StringId cue)
        : mSystem(&system), mCue(cue)
    {
    }

    void bind(GameAudioSystem& system, eng::StringId cue);
    bool play(glm::vec3 position, glm::vec3 velocity = glm::vec3(0.0f));
    void setTransform(glm::vec3 position, glm::vec3 velocity = glm::vec3(0.0f));
    void stop(eng::StopMode mode = eng::StopMode::AllowFadeOut);
    bool playing() const;
    eng::SoundInstance::Ptr voice() const { return mVoice; }

private:
    GameAudioSystem* mSystem = nullptr;
    eng::StringId mCue;
    eng::SoundInstance::Ptr mVoice;
};

} // namespace game
