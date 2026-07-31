#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace game {

inline constexpr std::uint16_t kMaxPatternTracks = 8;
inline constexpr std::uint16_t kMaxPatternRepeats = 64;
inline constexpr std::uint16_t kMaxShotsPerVolley = 64;
inline constexpr std::uint16_t kMaxShotsPerPattern = 512;
inline constexpr std::uint16_t kMaxPatternCommandsPerTick = 128;

enum class BulletPatternShape : std::uint8_t { Aimed, Fan, Ring };
enum class BulletAimMode : std::uint8_t { Facing, CommitTarget, LiveTarget };

struct BulletVolleyDef {
    std::string projectileId;
    BulletPatternShape shape = BulletPatternShape::Aimed;
    BulletAimMode aim = BulletAimMode::CommitTarget;
    std::uint16_t shotCount = 1;
    float arcRadians = 0.0f;
    float angleOffsetRadians = 0.0f;
};

struct BulletPatternTrack {
    std::uint16_t firstTick = 0;
    std::uint16_t intervalTicks = 0;
    std::uint16_t repetitions = 1;
    float rotationPerRepeatRadians = 0.0f;
    BulletVolleyDef volley;
};

struct BulletPatternDef {
    std::string id;
    std::vector<BulletPatternTrack> tracks;
};

struct BulletSpawnCommand {
    const std::string* projectileId = nullptr;
    glm::vec3 direction{0.0f, 0.0f, 1.0f};
    std::uint16_t trackIndex = 0;
    std::uint16_t repetitionIndex = 0;
    std::uint16_t shotIndex = 0;
};

struct BulletPatternStep {
    std::array<BulletSpawnCommand, kMaxPatternCommandsPerTick> commands{};
    std::uint16_t commandCount = 0;
    bool overflow = false;
};

struct BulletPatternState {
    bool active = false;
    std::uint16_t tick = 0;
    glm::vec3 commitAim{0.0f, 0.0f, 1.0f};
};

namespace bulletpattern {

std::uint16_t secondsToTicks(float seconds, float fixedDt);
bool validate(const BulletPatternDef& pattern, std::uint16_t activeTicks,
              std::string& error);
void begin(BulletPatternState& state, glm::vec3 commitAim);
BulletPatternStep advance(const BulletPatternDef& pattern,
                          BulletPatternState& state, glm::vec3 facing,
                          glm::vec3 liveAim);
void cancel(BulletPatternState& state);

} // namespace bulletpattern

enum class SpecialAttackPhase : std::uint8_t {
    Idle,
    Telegraph,
    Active,
    Recovery,
    Finished,
};

enum class SpecialAttackMarker : std::uint8_t {
    TelegraphStarted,
    Commit,
    RecoveryStarted,
    Finished,
    Cancelled,
};

struct SpecialAttackDef {
    std::string id;
    std::uint16_t telegraphTicks = 1;
    std::uint16_t activeTicks = 1;
    std::uint16_t recoveryTicks = 1;
    BulletPatternDef pattern;
};

struct SpecialAttackState {
    SpecialAttackPhase phase = SpecialAttackPhase::Idle;
    std::uint16_t phaseTick = 0;
    BulletPatternState pattern;
};

struct SpecialAttackStep {
    BulletPatternStep bullets;
    std::array<SpecialAttackMarker, 2> markers{};
    std::uint8_t markerCount = 0;
};

namespace specialattack {

bool validate(const SpecialAttackDef& definition, std::string& error);
SpecialAttackStep begin(const SpecialAttackDef& definition,
                        SpecialAttackState& state, glm::vec3 commitAim);
SpecialAttackStep advance(const SpecialAttackDef& definition,
                          SpecialAttackState& state, glm::vec3 facing,
                          glm::vec3 liveAim);
SpecialAttackStep cancel(SpecialAttackState& state);

} // namespace specialattack

} // namespace game
