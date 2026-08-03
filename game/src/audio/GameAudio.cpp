#include "GameAudio.h"

#include <eng/Config.h>
#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <utility>

namespace game {
namespace {

constexpr std::size_t busIndex(eng::AudioBus bus)
{
    return static_cast<std::size_t>(bus);
}

std::optional<eng::AudioBus> parseBus(const std::string& value)
{
    if (value == "master")
        return eng::AudioBus::Master;
    if (value == "music")
        return eng::AudioBus::Music;
    if (value == "ambience")
        return eng::AudioBus::Ambience;
    if (value == "dialogue" || value == "voice")
        return eng::AudioBus::Dialogue;
    if (value == "weapons")
        return eng::AudioBus::Weapons;
    if (value == "ui")
        return eng::AudioBus::Ui;
    if (value == "warnings")
        return eng::AudioBus::Warnings;
    if (value == "sfx")
        return eng::AudioBus::Sfx;
    return std::nullopt;
}

std::optional<eng::AudioPriority> parsePriority(const std::string& value)
{
    if (value == "background")
        return eng::AudioPriority::Background;
    if (value == "low")
        return eng::AudioPriority::Low;
    if (value == "important")
        return eng::AudioPriority::Important;
    if (value == "critical")
        return eng::AudioPriority::Critical;
    if (value == "normal")
        return eng::AudioPriority::Normal;
    return std::nullopt;
}

float number(const toml::table& table, const char* key, float fallback)
{
    const float value = float(table[key].value_or(double(fallback)));
    return std::isfinite(value) ? value : fallback;
}

void resetCatalog(AudioCatalog& out)
{
    out = {};
    out.busGainDb.fill(0.0f);
    out.busGainDb[busIndex(eng::AudioBus::Master)] = -4.0f;
    out.maxVoices = 96;
    out.dialogueDuckDb = -9.0f;
    out.duckAttackSeconds = 0.025f;
    out.duckReleaseSeconds = 0.45f;
}

bool parseTable(const toml::table& root, AudioCatalog& out)
{
    resetCatalog(out);

    if (const toml::table* mixer = root["mixer"].as_table()) {
        out.maxVoices = std::size_t(std::clamp<std::int64_t>(
            (*mixer)["max_voices"].value_or<std::int64_t>(96), 8, 512));
        const std::pair<const char*, eng::AudioBus> gains[] = {
            {"master_db", eng::AudioBus::Master},
            {"music_db", eng::AudioBus::Music},
            {"ambience_db", eng::AudioBus::Ambience},
            {"dialogue_db", eng::AudioBus::Dialogue},
            {"weapons_db", eng::AudioBus::Weapons},
            {"sfx_db", eng::AudioBus::Sfx},
            {"ui_db", eng::AudioBus::Ui},
            {"warnings_db", eng::AudioBus::Warnings},
        };
        for (const auto& [key, bus] : gains)
            out.busGainDb[busIndex(bus)] =
                std::clamp(number(*mixer, key, out.busGainDb[busIndex(bus)]),
                           -80.0f, 12.0f);
    }

    if (const toml::table* duck = root["ducking"].as_table()) {
        out.dialogueDuckDb = std::clamp(
            number(*duck, "music_under_dialogue_db", -9.0f), -30.0f, 0.0f);
        out.duckAttackSeconds =
            std::max(0.0f, number(*duck, "attack_seconds", 0.025f));
        out.duckReleaseSeconds =
            std::max(0.0f, number(*duck, "release_seconds", 0.45f));
    }

    if (const toml::array* cues = root["cue"].as_array()) {
        for (const toml::node& node : *cues) {
            const toml::table* table = node.as_table();
            if (!table)
                continue;
            AudioCueDefinition cue;
            cue.name = (*table)["id"].value_or(std::string{});
            if (cue.name.empty()) {
                eng::log::warn("AudioCatalog: cue without id skipped");
                continue;
            }
            cue.id = eng::intern(cue.name);
            if (const toml::array* files = (*table)["files"].as_array()) {
                for (const toml::node& file : *files)
                    if (auto value = file.value<std::string>(); value)
                        cue.samples.push_back(*value);
            }
            const std::string bus =
                (*table)["bus"].value_or(std::string{"sfx"});
            const std::string priority =
                (*table)["priority"].value_or(std::string{"normal"});
            const std::string limit =
                (*table)["limit"].value_or(std::string{"steal_oldest"});
            const auto parsedBus = parseBus(bus);
            const auto parsedPriority = parsePriority(priority);
            if (!parsedBus || !parsedPriority ||
                (limit != "reject" && limit != "steal_oldest")) {
                eng::log::error("AudioCatalog: cue '%s' has invalid bus, "
                                "priority, or limit",
                                cue.name.c_str());
                continue;
            }
            cue.bus = *parsedBus;
            cue.gainDb =
                std::clamp(number(*table, "gain_db", 0.0f), -80.0f, 12.0f);
            cue.gainVariationDb = std::clamp(
                number(*table, "gain_variation_db", 0.0f), 0.0f, 12.0f);
            cue.pitchMin =
                std::clamp(number(*table, "pitch_min", 1.0f), 0.25f, 4.0f);
            cue.pitchMax = std::clamp(number(*table, "pitch_max", 1.0f),
                                      cue.pitchMin, 4.0f);
            cue.spatialized = (*table)["spatial"].value_or(true);
            cue.streaming = (*table)["stream"].value_or(false);
            cue.looping = (*table)["loop"].value_or(false);
            cue.minDistance = std::max(
                0.01f, number(*table, "min_distance", cue.minDistance));
            cue.maxDistance =
                std::max(cue.minDistance,
                         number(*table, "max_distance", cue.maxDistance));
            cue.rolloff =
                std::max(0.0f, number(*table, "rolloff", cue.rolloff));
            cue.dopplerFactor = std::max(
                0.0f, number(*table, "doppler_factor", cue.dopplerFactor));
            cue.maxInstances = std::size_t(std::clamp<std::int64_t>(
                (*table)["max_instances"].value_or<std::int64_t>(8), 1, 128));
            cue.cooldownSeconds =
                std::max(0.0f, number(*table, "cooldown_seconds", 0.0f));
            cue.priority = *parsedPriority;
            cue.limitPolicy = limit == "reject" ? VoiceLimitPolicy::Reject
                                                : VoiceLimitPolicy::StealOldest;
            cue.duckMusicDb = std::clamp(
                number(*table, "duck_music_db",
                       cue.bus == eng::AudioBus::Dialogue ? out.dialogueDuckDb
                                                          : 0.0f),
                -30.0f, 0.0f);
            const std::string cueName = cue.name;
            if (!out.cues.emplace(cue.id, std::move(cue)).second)
                eng::log::error("AudioCatalog: duplicate cue '%s'",
                                cueName.c_str());
        }
    }

    if (const toml::table* music = root["music"].as_table()) {
        out.music.bpm = std::clamp(number(*music, "bpm", 80.0f), 20.0f, 300.0f);
        out.music.beatsPerBar = int(std::clamp<std::int64_t>(
            (*music)["beats_per_bar"].value_or<std::int64_t>(4), 1, 16));
        out.music.transitionBars = int(std::clamp<std::int64_t>(
            (*music)["transition_bars"].value_or<std::int64_t>(1), 1, 8));
        out.music.transitionSeconds =
            std::max(0.0f, number(*music, "transition_seconds", 0.35f));
        out.music.intensityRiseSeconds =
            std::max(0.01f, number(*music, "intensity_rise_seconds", 0.8f));
        out.music.intensityFallSeconds =
            std::max(0.01f, number(*music, "intensity_fall_seconds", 3.5f));
        out.music.defaultSection = eng::intern(
            (*music)["default_section"].value_or(std::string{"exploration"}));
        const std::string combatStinger =
            (*music)["combat_stinger"].value_or(std::string{});
        const std::string bossStinger =
            (*music)["boss_stinger"].value_or(std::string{});
        if (!combatStinger.empty())
            out.music.combatStinger = eng::intern(combatStinger);
        if (!bossStinger.empty())
            out.music.bossStinger = eng::intern(bossStinger);
    }

    if (const toml::array* stems = root["music_stem"].as_array()) {
        for (const toml::node& node : *stems) {
            const toml::table* table = node.as_table();
            if (!table)
                continue;
            const std::string section =
                (*table)["section"].value_or(std::string{});
            const std::string cue = (*table)["cue"].value_or(std::string{});
            if (section.empty() || cue.empty()) {
                eng::log::warn(
                    "AudioCatalog: music stem missing section or cue, skipped");
                continue;
            }
            MusicStemDefinition stem;
            stem.section = eng::intern(section);
            stem.cue = eng::intern(cue);
            stem.startsAt =
                std::clamp(number(*table, "starts_at", 0.0f), 0.0f, 1.0f);
            stem.fullAt = std::clamp(number(*table, "full_at", 1.0f),
                                     stem.startsAt, 1.0f);
            stem.maxGainDb =
                std::clamp(number(*table, "max_gain_db", 0.0f), -30.0f, 6.0f);
            out.music.stems.push_back(stem);
        }
    }

    for (const MusicStemDefinition& stem : out.music.stems) {
        const AudioCueDefinition* cue = out.cue(stem.cue);
        if (!cue) {
            eng::log::error("AudioCatalog: music stem names missing cue '%s'",
                            stem.cue.c_str());
            return false;
        }
        if (cue->bus != eng::AudioBus::Music || !cue->looping) {
            eng::log::error(
                "AudioCatalog: stem cue '%s' must be looping on music bus",
                stem.cue.c_str());
            return false;
        }
    }
    const auto requireMusicCue = [&](eng::StringId cue, const char* role) {
        if (cue && !out.cue(cue)) {
            eng::log::error("AudioCatalog: %s names missing cue '%s'", role,
                            cue.c_str());
            return false;
        }
        return true;
    };
    if (!requireMusicCue(out.music.combatStinger, "combat stinger") ||
        !requireMusicCue(out.music.bossStinger, "boss stinger"))
        return false;
    if (!out.music.stems.empty() && out.music.defaultSection &&
        std::none_of(out.music.stems.begin(), out.music.stems.end(),
                     [&](const MusicStemDefinition& stem) {
                         return stem.section == out.music.defaultSection;
                     })) {
        eng::log::error("AudioCatalog: default music section '%s' has no stems",
                        out.music.defaultSection.c_str());
        return false;
    }
    return !out.cues.empty();
}

float smoothStep(float edge0, float edge1, float value)
{
    if (edge1 <= edge0)
        return value >= edge0 ? 1.0f : 0.0f;
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

const AudioCueDefinition* AudioCatalog::cue(eng::StringId id) const
{
    const auto it = cues.find(id);
    return it == cues.end() ? nullptr : &it->second;
}

bool parseAudioCatalog(const std::string& tomlPath, AudioCatalog& out)
{
    const toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("AudioCatalog: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        return false;
    }
    return parseTable(parsed.table(), out);
}

bool parseAudioCatalogText(std::string_view source, AudioCatalog& out)
{
    const toml::parse_result parsed = toml::parse(source);
    if (!parsed) {
        eng::log::error("AudioCatalog: %s",
                        std::string(parsed.error().description()).c_str());
        return false;
    }
    return parseTable(parsed.table(), out);
}

float targetMusicIntensity(const MusicMixState& state)
{
    const float threat = std::isfinite(state.threat) ? state.threat : 0.0f;
    const float authoredHealth =
        std::isfinite(state.playerHealth) ? state.playerHealth : 1.0f;
    float intensity = std::clamp(threat, 0.0f, 1.0f);
    const float health = std::clamp(authoredHealth, 0.0f, 1.0f);
    if (intensity > 0.05f)
        intensity = std::min(1.0f, intensity + (1.0f - health) * 0.18f);
    if (state.carryingSignificantObject)
        intensity = std::max(intensity, 0.28f);
    if (state.extracting)
        intensity = std::max(intensity, 0.72f);
    if (state.boss)
        intensity = 1.0f;
    return intensity;
}

std::uint64_t quantizeAudioFrame(std::uint64_t now, std::uint32_t sampleRate,
                                 float bpm, int beatsPerBar, int bars)
{
    if (sampleRate == 0 || !std::isfinite(bpm) || bpm <= 0.0f ||
        beatsPerBar <= 0 || bars <= 0)
        return now;
    const double framesPerBoundary =
        double(sampleRate) * 60.0 / double(bpm) * double(beatsPerBar * bars);
    const auto boundary = static_cast<std::uint64_t>(
        std::max(1.0, std::round(framesPerBoundary)));
    return (now / boundary + 1) * boundary;
}

struct GameAudioSystem::Impl {
    struct CueRuntime {
        double lastStart = -std::numeric_limits<double>::infinity();
        std::size_t lastSample = std::numeric_limits<std::size_t>::max();
    };
    struct ActiveVoice {
        eng::StringId cue;
        eng::SoundInstance::Ptr voice;
        std::uint64_t serial = 0;
        float duckMusicDb = 0.0f;
    };
    struct ActiveStem {
        MusicStemDefinition definition;
        eng::SoundInstance::Ptr voice;
    };

    eng::Audio* audio = nullptr;
    AudioCatalog catalog;
    std::unordered_map<eng::StringId, CueRuntime> cueRuntime;
    std::vector<ActiveVoice> voices;
    std::vector<ActiveStem> stems;
    std::uint32_t rng = 1;
    std::uint64_t serial = 0;
    double time = 0.0;
    glm::vec3 listenerPosition{0.0f};
    glm::vec3 previousListenerPosition{0.0f};
    bool haveListener = false;
    PlayerFoleyState previousFoley;
    bool haveFoley = false;
    float strideDistance = 0.0f;
    float musicIntensity = 0.0f;
    int musicTier = 0;
    bool bossWasActive = false;
    eng::StringId musicSection;
    std::uint64_t musicOriginFrame = 0;
    GameAudioStats counters;

    std::uint32_t nextRandom()
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    float random01()
    {
        return float(nextRandom() & 0x00FFFFFFu) / float(0x01000000u);
    }

    bool hasSection(eng::StringId section) const
    {
        return std::any_of(catalog.music.stems.begin(),
                           catalog.music.stems.end(),
                           [section](const MusicStemDefinition& stem) {
                               return stem.section == section;
                           });
    }
};

GameAudioSystem::GameAudioSystem() : mImpl(std::make_unique<Impl>()) {}
GameAudioSystem::~GameAudioSystem()
{
    stopAll(eng::StopMode::Immediate);
}

bool GameAudioSystem::load(eng::Audio& audio, const std::string& catalogPath,
                           std::uint32_t randomSeed)
{
    stopAll(eng::StopMode::Immediate);
    AudioCatalog parsed;
    if (!parseAudioCatalog(catalogPath, parsed))
        return false;

    std::size_t unavailable = 0;
    for (auto& [id, cue] : parsed.cues) {
        std::vector<std::string> resolved;
        resolved.reserve(cue.samples.size());
        for (const std::string& authored : cue.samples) {
            std::error_code error;
            if (std::filesystem::exists(authored, error)) {
                resolved.push_back(authored);
                continue;
            }
            const std::filesystem::path path = eng::assets::resolve(authored);
            if (!path.empty())
                resolved.push_back(path.string());
        }
        cue.samples = std::move(resolved);
        if (cue.samples.empty())
            ++unavailable;
    }

    mImpl->audio = &audio;
    mImpl->catalog = std::move(parsed);
    mImpl->cueRuntime.clear();
    mImpl->voices.clear();
    mImpl->voices.reserve(mImpl->catalog.maxVoices);
    mImpl->stems.clear();
    mImpl->rng = randomSeed ? randomSeed : 1u;
    mImpl->time = 0.0;
    mImpl->serial = 0;
    mImpl->musicIntensity = 0.0f;
    mImpl->musicTier = 0;
    mImpl->musicSection = {};
    mImpl->musicOriginFrame = 0;
    mImpl->counters = {};

    audio.setMaxVoices(mImpl->catalog.maxVoices);
    for (std::size_t i = 0; i < eng::kAudioBusCount; ++i)
        audio.setBusGainDb(static_cast<eng::AudioBus>(i),
                           mImpl->catalog.busGainDb[i]);

    eng::log::info("AudioCatalog: %zu cues, %zu stems, %zu awaiting assets",
                   mImpl->catalog.cues.size(),
                   mImpl->catalog.music.stems.size(), unavailable);
    if (mImpl->catalog.music.defaultSection)
        requestMusicSection(mImpl->catalog.music.defaultSection);
    return true;
}

void GameAudioSystem::applyUserMix(const eng::Config& config)
{
    if (!mImpl->audio)
        return;
    const std::pair<const char*, eng::AudioBus> values[] = {
        {"audio.master_db", eng::AudioBus::Master},
        {"audio.music_db", eng::AudioBus::Music},
        {"audio.ambience_db", eng::AudioBus::Ambience},
        {"audio.dialogue_db", eng::AudioBus::Dialogue},
        {"audio.weapons_db", eng::AudioBus::Weapons},
        {"audio.sfx_db", eng::AudioBus::Sfx},
        {"audio.ui_db", eng::AudioBus::Ui},
        {"audio.warnings_db", eng::AudioBus::Warnings},
    };
    for (const auto& [key, bus] : values) {
        const float authored = mImpl->catalog.busGainDb[busIndex(bus)];
        mImpl->audio->setBusGainDb(
            bus, float(config.getNumber(key, double(authored))));
    }
    const double configuredVoices =
        config.getNumber("audio.max_voices", double(mImpl->catalog.maxVoices));
    mImpl->audio->setMaxVoices(std::size_t(std::clamp(
        std::isfinite(configuredVoices) ? configuredVoices
                                        : double(mImpl->catalog.maxVoices),
        8.0, 512.0)));
}

eng::SoundInstance::Ptr GameAudioSystem::emit(eng::StringId cueId,
                                              const AudioEmission& emission)
{
    if (!mImpl->audio || !mImpl->audio->ready())
        return nullptr;
    const AudioCueDefinition* cue = mImpl->catalog.cue(cueId);
    if (!cue || cue->samples.empty()) {
        ++mImpl->counters.unavailableRejected;
        return nullptr;
    }

    const bool spatial =
        emission.spatial == SpatialOverride::Force3D ||
        (emission.spatial == SpatialOverride::CueDefault && cue->spatialized);
    if (spatial && mImpl->haveListener &&
        glm::distance(emission.position, mImpl->listenerPosition) >
            cue->maxDistance * 1.1f) {
        ++mImpl->counters.distanceCulled;
        return nullptr;
    }

    Impl::CueRuntime& runtime = mImpl->cueRuntime[cueId];
    if (mImpl->time - runtime.lastStart < cue->cooldownSeconds) {
        ++mImpl->counters.cooldownRejected;
        return nullptr;
    }

    std::size_t liveCount = 0;
    auto oldest = mImpl->voices.end();
    for (auto it = mImpl->voices.begin(); it != mImpl->voices.end(); ++it) {
        if (it->cue != cueId || !it->voice || it->voice->finished())
            continue;
        ++liveCount;
        if (oldest == mImpl->voices.end() || it->serial < oldest->serial)
            oldest = it;
    }
    if (liveCount >= cue->maxInstances) {
        if (cue->limitPolicy == VoiceLimitPolicy::Reject ||
            oldest == mImpl->voices.end()) {
            ++mImpl->counters.concurrencyRejected;
            return nullptr;
        }
        oldest->voice->stop(eng::StopMode::AllowFadeOut);
        mImpl->voices.erase(oldest);
    }

    std::size_t sample = std::size_t(mImpl->random01() * cue->samples.size());
    sample = std::min(sample, cue->samples.size() - 1);
    if (cue->samples.size() > 1 && sample == runtime.lastSample)
        sample =
            (sample + 1 +
             (mImpl->nextRandom() % std::uint32_t(cue->samples.size() - 1))) %
            cue->samples.size();

    eng::PlaybackSettings settings;
    settings.bus = cue->bus;
    settings.gainDb = cue->gainDb + emission.gainDb +
                      (mImpl->random01() * 2.0f - 1.0f) * cue->gainVariationDb;
    settings.pitch = std::clamp(
        (cue->pitchMin + (cue->pitchMax - cue->pitchMin) * mImpl->random01()) *
            emission.pitch,
        0.25f, 4.0f);
    settings.loop = cue->looping;
    settings.streaming = cue->streaming;
    settings.spatialized = spatial;
    settings.position = emission.position;
    settings.velocity = emission.velocity;
    settings.minDistance = cue->minDistance;
    settings.maxDistance = cue->maxDistance;
    settings.rolloff = cue->rolloff;
    settings.dopplerFactor = cue->dopplerFactor;
    settings.fadeInSeconds = std::max(0.0f, emission.fadeInSeconds);
    settings.startFrame = emission.startFrame;
    settings.priority = cue->priority;
    settings.stealable = cue->priority != eng::AudioPriority::Critical;

    eng::SoundInstance::Ptr voice =
        mImpl->audio->play(cue->samples[sample], settings);
    if (!voice)
        return nullptr;
    runtime.lastStart = mImpl->time;
    runtime.lastSample = sample;
    mImpl->voices.push_back({cueId, voice, ++mImpl->serial, cue->duckMusicDb});
    ++mImpl->counters.emitted;
    return voice;
}

void GameAudioSystem::setListener(glm::vec3 position, glm::vec3 forward,
                                  float realDt)
{
    if (!mImpl->audio)
        return;
    eng::AudioListener listener;
    listener.position = position;
    const float forwardLength = glm::length(forward);
    if (forwardLength > 0.0001f)
        listener.forward = forward / forwardLength;
    if (mImpl->haveListener && realDt > 0.0001f) {
        const glm::vec3 movement = position - mImpl->previousListenerPosition;
        if (glm::length(movement) < 5.0f)
            listener.velocity = movement / realDt;
    }
    mImpl->listenerPosition = position;
    mImpl->previousListenerPosition = position;
    mImpl->haveListener = true;
    mImpl->audio->setListener(listener);
}

void GameAudioSystem::updatePlayerFoley(const PlayerFoleyState& state)
{
    if (!mImpl->haveFoley) {
        mImpl->previousFoley = state;
        mImpl->haveFoley = true;
        return;
    }

    const glm::vec2 delta(state.feet.x - mImpl->previousFoley.feet.x,
                          state.feet.z - mImpl->previousFoley.feet.z);
    const float distance = glm::length(delta);
    if (distance > 4.0f) {
        mImpl->strideDistance = 0.0f; // teleport/level transition
    }
    else if (state.grounded && state.horizontalSpeed > 0.7f) {
        mImpl->strideDistance += distance;
        const float stride = state.sprinting ? 1.35f : 1.65f;
        if (mImpl->strideDistance >= stride) {
            AudioEmission emission;
            emission.position = state.feet;
            emission.spatial = SpatialOverride::Force2D;
            emit("player.footstep", emission);
            mImpl->strideDistance = std::fmod(mImpl->strideDistance, stride);
        }
    }

    if (!mImpl->previousFoley.grounded && state.grounded) {
        AudioEmission emission;
        emission.position = state.feet;
        emission.spatial = SpatialOverride::Force2D;
        emission.gainDb = std::clamp(state.horizontalSpeed * 0.3f, 0.0f, 3.0f);
        emit("player.land", emission);
    }
    mImpl->previousFoley = state;
}

void GameAudioSystem::requestMusicSection(eng::StringId section)
{
    if (!mImpl->audio || !section || section == mImpl->musicSection ||
        !mImpl->hasSection(section))
        return;

    const MusicDefinition& music = mImpl->catalog.music;
    const std::uint64_t now = mImpl->audio->clockFrame();
    std::uint64_t startFrame = 0;
    if (mImpl->musicSection) {
        const std::uint64_t relativeNow =
            now > mImpl->musicOriginFrame ? now - mImpl->musicOriginFrame : 0;
        startFrame = mImpl->musicOriginFrame +
                     quantizeAudioFrame(relativeNow, mImpl->audio->sampleRate(),
                                        music.bpm, music.beatsPerBar,
                                        music.transitionBars);
    }
    else {
        startFrame = now + std::uint64_t(mImpl->audio->sampleRate() / 20u);
        mImpl->musicOriginFrame = startFrame;
    }
    const std::uint64_t fadeFrames = std::uint64_t(
        music.transitionSeconds * float(mImpl->audio->sampleRate()));

    for (Impl::ActiveStem& stem : mImpl->stems) {
        if (stem.voice && !stem.voice->finished() &&
            stem.definition.section == mImpl->musicSection)
            stem.voice->scheduleStop(startFrame + fadeFrames,
                                     music.transitionSeconds);
    }

    for (const MusicStemDefinition& definition : music.stems) {
        if (definition.section != section)
            continue;
        AudioEmission emission;
        emission.spatial = SpatialOverride::Force2D;
        emission.startFrame = startFrame;
        emission.fadeInSeconds = music.transitionSeconds;
        const float activation = smoothStep(
            definition.startsAt, definition.fullAt, mImpl->musicIntensity);
        const float initialGain =
            -60.0f + (definition.maxGainDb + 60.0f) * activation;
        const AudioCueDefinition* cue = mImpl->catalog.cue(definition.cue);
        emission.gainDb = initialGain - (cue ? cue->gainDb : 0.0f);
        eng::SoundInstance::Ptr voice = emit(definition.cue, emission);
        if (!voice)
            continue;
        mImpl->stems.push_back({definition, std::move(voice)});
    }
    mImpl->musicSection = section;
}

void GameAudioSystem::update(float realDt, const MusicMixState& musicState)
{
    if (!mImpl->audio)
        return;
    const float dt =
        std::isfinite(realDt) ? std::clamp(realDt, 0.0f, 0.25f) : 0.0f;
    mImpl->time += dt;

    mImpl->voices.erase(
        std::remove_if(mImpl->voices.begin(), mImpl->voices.end(),
                       [](const Impl::ActiveVoice& active) {
                           return !active.voice || active.voice->finished();
                       }),
        mImpl->voices.end());
    mImpl->stems.erase(std::remove_if(mImpl->stems.begin(), mImpl->stems.end(),
                                      [](const Impl::ActiveStem& stem) {
                                          return !stem.voice ||
                                                 stem.voice->finished();
                                      }),
                       mImpl->stems.end());

    float duckDb = 0.0f;
    for (const Impl::ActiveVoice& active : mImpl->voices)
        if (active.voice && active.voice->isPlaying())
            duckDb = std::min(duckDb, active.duckMusicDb);
    mImpl->audio->setBusDuckDb(eng::AudioBus::Music, duckDb,
                               mImpl->catalog.duckAttackSeconds,
                               mImpl->catalog.duckReleaseSeconds);
    mImpl->audio->setBusDuckDb(eng::AudioBus::Ambience, duckDb * 0.55f,
                               mImpl->catalog.duckAttackSeconds,
                               mImpl->catalog.duckReleaseSeconds);

    const float target = targetMusicIntensity(musicState);
    const float seconds = target > mImpl->musicIntensity
                              ? mImpl->catalog.music.intensityRiseSeconds
                              : mImpl->catalog.music.intensityFallSeconds;
    const float alpha = dt <= 0.0f ? 0.0f : 1.0f - std::exp(-dt / seconds);
    mImpl->musicIntensity += (target - mImpl->musicIntensity) * alpha;

    const int previousTier = mImpl->musicTier;
    if (mImpl->musicTier == 0 && mImpl->musicIntensity >= 0.42f)
        mImpl->musicTier = 1;
    else if (mImpl->musicTier == 1 && mImpl->musicIntensity <= 0.25f)
        mImpl->musicTier = 0;
    if (previousTier == 0 && mImpl->musicTier == 1 &&
        mImpl->catalog.music.combatStinger) {
        AudioEmission stinger;
        stinger.spatial = SpatialOverride::Force2D;
        emit(mImpl->catalog.music.combatStinger, stinger);
    }
    if (!mImpl->bossWasActive && musicState.boss &&
        mImpl->catalog.music.bossStinger) {
        AudioEmission stinger;
        stinger.spatial = SpatialOverride::Force2D;
        emit(mImpl->catalog.music.bossStinger, stinger);
    }
    mImpl->bossWasActive = musicState.boss;

    eng::StringId desired = mImpl->catalog.music.defaultSection;
    const eng::StringId exploration = audioCueId("exploration");
    const eng::StringId combat = audioCueId("combat");
    const eng::StringId boss = audioCueId("boss");
    const eng::StringId extraction = audioCueId("extraction");
    if (musicState.boss && mImpl->hasSection(boss))
        desired = boss;
    else if (musicState.extracting && mImpl->hasSection(extraction))
        desired = extraction;
    else if (mImpl->musicTier > 0 && mImpl->hasSection(combat))
        desired = combat;
    else if (mImpl->hasSection(exploration))
        desired = exploration;
    requestMusicSection(desired);

    for (Impl::ActiveStem& stem : mImpl->stems) {
        if (!stem.voice || stem.voice->finished() ||
            stem.definition.section != mImpl->musicSection)
            continue;
        const float activation =
            smoothStep(stem.definition.startsAt, stem.definition.fullAt,
                       mImpl->musicIntensity);
        const float gainDb =
            -60.0f + (stem.definition.maxGainDb + 60.0f) * activation;
        stem.voice->setGainDb(gainDb);
    }
    mImpl->counters.musicIntensity = mImpl->musicIntensity;
    mImpl->counters.musicTier = mImpl->musicTier;
}

void GameAudioSystem::stopAll(eng::StopMode mode)
{
    if (!mImpl)
        return;
    for (Impl::ActiveVoice& active : mImpl->voices)
        if (active.voice)
            active.voice->stop(mode);
    mImpl->voices.clear();
    mImpl->stems.clear();
    mImpl->musicSection = {};
    mImpl->musicOriginFrame = 0;
    mImpl->haveFoley = false;
    mImpl->strideDistance = 0.0f;
}

const AudioCatalog& GameAudioSystem::catalog() const
{
    return mImpl->catalog;
}

const AudioCueDefinition* GameAudioSystem::cue(eng::StringId id) const
{
    return mImpl->catalog.cue(id);
}

GameAudioStats GameAudioSystem::stats() const
{
    GameAudioStats result = mImpl->counters;
    if (mImpl->audio)
        result.backend = mImpl->audio->stats();
    return result;
}

bool GameAudioSystem::loaded() const
{
    return mImpl->audio && !mImpl->catalog.cues.empty();
}

void AudioEmitter::bind(GameAudioSystem& system, eng::StringId cue)
{
    stop(eng::StopMode::Immediate);
    mSystem = &system;
    mCue = cue;
}

bool AudioEmitter::play(glm::vec3 position, glm::vec3 velocity)
{
    if (!mSystem || !mCue)
        return false;
    const AudioCueDefinition* definition = mSystem->cue(mCue);
    if (mVoice && !mVoice->finished() && definition && definition->looping) {
        setTransform(position, velocity);
        return true;
    }
    AudioEmission emission;
    emission.position = position;
    emission.velocity = velocity;
    mVoice = mSystem->emit(mCue, emission);
    return mVoice != nullptr;
}

void AudioEmitter::setTransform(glm::vec3 position, glm::vec3 velocity)
{
    if (!mVoice || mVoice->finished())
        return;
    mVoice->setPosition(position);
    mVoice->setVelocity(velocity);
}

void AudioEmitter::stop(eng::StopMode mode)
{
    if (mVoice)
        mVoice->stop(mode);
    mVoice.reset();
}

bool AudioEmitter::playing() const
{
    return mVoice && !mVoice->finished();
}

} // namespace game
