#include "BulletPattern.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace game {
namespace {

glm::vec3 safeDirection(glm::vec3 value, glm::vec3 fallback)
{
    const auto finite = [](glm::vec3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) &&
               std::isfinite(v.z);
    };
    if (!finite(value) || glm::dot(value, value) < 0.000001f)
        value = fallback;
    if (!finite(value) || glm::dot(value, value) < 0.000001f)
        value = glm::vec3(0.0f, 0.0f, 1.0f);
    return glm::normalize(value);
}

glm::vec3 selectedAim(BulletAimMode mode, const BulletPatternState& state,
                      glm::vec3 facing, glm::vec3 liveAim)
{
    if (mode == BulletAimMode::CommitTarget)
        return safeDirection(state.commitAim, facing);
    if (mode == BulletAimMode::LiveTarget)
        return safeDirection(liveAim, facing);
    return safeDirection(facing, glm::vec3(0, 0, 1));
}

void push(BulletPatternStep& output, const std::string& projectileId,
          glm::vec3 direction, std::uint16_t track,
          std::uint16_t repetition, std::uint16_t shot)
{
    if (output.commandCount >= output.commands.size()) {
        output.overflow = true;
        return;
    }
    output.commands[output.commandCount++] = {
        &projectileId, safeDirection(direction, glm::vec3(0, 0, 1)), track,
        repetition, shot};
}

void emitVolley(BulletPatternStep& output, const BulletPatternTrack& track,
                const BulletPatternState& state, glm::vec3 facing,
                glm::vec3 liveAim, std::uint16_t trackIndex,
                std::uint16_t repetition)
{
    const BulletVolleyDef& volley = track.volley;
    glm::vec3 aim = selectedAim(volley.aim, state, facing, liveAim);
    const float rotation = volley.angleOffsetRadians +
                           track.rotationPerRepeatRadians * float(repetition);

    if (volley.shape == BulletPatternShape::Ring) {
        aim.y = 0.0f;
        aim = safeDirection(aim, facing);
        for (std::uint16_t i = 0; i < volley.shotCount; ++i) {
            const float angle = rotation + glm::two_pi<float>() *
                                               float(i) /
                                               float(volley.shotCount);
            push(output, volley.projectileId,
                 glm::angleAxis(angle, glm::vec3(0, 1, 0)) * aim, trackIndex,
                 repetition, i);
        }
        return;
    }

    if (volley.shape == BulletPatternShape::Aimed) {
        push(output, volley.projectileId,
             glm::angleAxis(rotation, glm::vec3(0, 1, 0)) * aim, trackIndex,
             repetition, 0);
        return;
    }

    for (std::uint16_t i = 0; i < volley.shotCount; ++i) {
        const float alpha = volley.shotCount == 1
                                ? 0.5f
                                : float(i) / float(volley.shotCount - 1);
        const float angle = rotation + (alpha - 0.5f) * volley.arcRadians;
        push(output, volley.projectileId,
             glm::angleAxis(angle, glm::vec3(0, 1, 0)) * aim, trackIndex,
             repetition, i);
    }
}

void marker(SpecialAttackStep& output, SpecialAttackMarker value)
{
    if (output.markerCount < output.markers.size())
        output.markers[output.markerCount++] = value;
}

} // namespace

namespace bulletpattern {

std::uint16_t secondsToTicks(float seconds, float fixedDt)
{
    if (!std::isfinite(seconds) || !std::isfinite(fixedDt) || seconds <= 0.0f ||
        fixedDt <= 0.0f)
        return 0;
    return std::uint16_t(std::clamp(
        std::ceil(seconds / fixedDt), 0.0f,
        float(std::numeric_limits<std::uint16_t>::max())));
}

bool validate(const BulletPatternDef& pattern, std::uint16_t activeTicks,
              std::string& error)
{
    if (pattern.tracks.empty() || pattern.tracks.size() > kMaxPatternTracks) {
        error = "pattern needs 1..8 tracks";
        return false;
    }
    std::uint32_t totalShots = 0;
    std::vector<std::uint16_t> perTick(activeTicks, 0);
    for (std::size_t i = 0; i < pattern.tracks.size(); ++i) {
        const BulletPatternTrack& track = pattern.tracks[i];
        const BulletVolleyDef& volley = track.volley;
        if (!std::isfinite(track.rotationPerRepeatRadians) ||
            !std::isfinite(volley.arcRadians) ||
            !std::isfinite(volley.angleOffsetRadians)) {
            error = "pattern angles must be finite";
            return false;
        }
        if (track.repetitions == 0 || track.repetitions > kMaxPatternRepeats) {
            error = "track repeat count is outside 1..64";
            return false;
        }
        if (track.repetitions > 1 && track.intervalTicks == 0) {
            error = "repeated track needs a positive interval";
            return false;
        }
        if (volley.projectileId.empty() || volley.shotCount == 0 ||
            volley.shotCount > kMaxShotsPerVolley) {
            error = "volley needs projectile id and 1..64 shots";
            return false;
        }
        if (volley.shape == BulletPatternShape::Aimed &&
            volley.shotCount != 1) {
            error = "aimed volley emits exactly one shot";
            return false;
        }
        if (volley.shape == BulletPatternShape::Fan &&
            (volley.arcRadians <= 0.0f ||
             volley.arcRadians >= glm::two_pi<float>())) {
            error = "fan arc must be between zero and one turn";
            return false;
        }
        if (volley.shape == BulletPatternShape::Ring && volley.shotCount < 2) {
            error = "ring needs at least two shots";
            return false;
        }
        const std::uint32_t last =
            std::uint32_t(track.firstTick) +
            std::uint32_t(track.intervalTicks) * (track.repetitions - 1);
        if (last >= activeTicks) {
            error = "track extends beyond active window";
            return false;
        }
        totalShots += std::uint32_t(volley.shotCount) * track.repetitions;
        if (totalShots > kMaxShotsPerPattern) {
            error = "pattern exceeds 512 shots";
            return false;
        }
        for (std::uint16_t repeat = 0; repeat < track.repetitions; ++repeat) {
            const std::uint32_t tick = track.firstTick +
                                       track.intervalTicks * repeat;
            const std::uint32_t count = perTick[tick] + volley.shotCount;
            if (count > kMaxPatternCommandsPerTick) {
                error = "pattern exceeds per-tick command budget";
                return false;
            }
            perTick[tick] = std::uint16_t(count);
        }
    }
    error.clear();
    return true;
}

void begin(BulletPatternState& state, glm::vec3 commitAim)
{
    state.active = true;
    state.tick = 0;
    state.commitAim = safeDirection(commitAim, glm::vec3(0, 0, 1));
}

BulletPatternStep advance(const BulletPatternDef& pattern,
                          BulletPatternState& state, glm::vec3 facing,
                          glm::vec3 liveAim)
{
    BulletPatternStep output;
    if (!state.active)
        return output;
    for (std::uint16_t trackIndex = 0;
         trackIndex < pattern.tracks.size(); ++trackIndex) {
        const BulletPatternTrack& track = pattern.tracks[trackIndex];
        for (std::uint16_t repeat = 0; repeat < track.repetitions; ++repeat) {
            const std::uint32_t scheduled = track.firstTick +
                                            track.intervalTicks * repeat;
            if (scheduled == state.tick)
                emitVolley(output, track, state, facing, liveAim, trackIndex,
                           repeat);
        }
    }
    if (state.tick < std::numeric_limits<std::uint16_t>::max())
        ++state.tick;
    return output;
}

void cancel(BulletPatternState& state)
{
    state.active = false;
    state.tick = 0;
}

} // namespace bulletpattern

namespace specialattack {

bool validate(const SpecialAttackDef& definition, std::string& error)
{
    if (definition.id.empty()) {
        error = "special attack needs an id";
        return false;
    }
    if (definition.activeTicks == 0 || definition.recoveryTicks == 0) {
        error = "special attack needs active and recovery ticks";
        return false;
    }
    return bulletpattern::validate(definition.pattern, definition.activeTicks,
                                   error);
}

SpecialAttackStep begin(const SpecialAttackDef& definition,
                        SpecialAttackState& state, glm::vec3 commitAim)
{
    state.phase = SpecialAttackPhase::Telegraph;
    state.phaseTick = 0;
    state.pattern.commitAim = safeDirection(commitAim, glm::vec3(0, 0, 1));
    SpecialAttackStep output;
    marker(output, SpecialAttackMarker::TelegraphStarted);
    if (definition.telegraphTicks == 0) {
        state.phase = SpecialAttackPhase::Active;
        bulletpattern::begin(state.pattern, commitAim);
        marker(output, SpecialAttackMarker::Commit);
    }
    return output;
}

SpecialAttackStep advance(const SpecialAttackDef& definition,
                          SpecialAttackState& state, glm::vec3 facing,
                          glm::vec3 liveAim)
{
    SpecialAttackStep output;
    if (state.phase == SpecialAttackPhase::Telegraph) {
        if (++state.phaseTick >= definition.telegraphTicks) {
            state.phase = SpecialAttackPhase::Active;
            state.phaseTick = 0;
            bulletpattern::begin(state.pattern, state.pattern.commitAim);
            marker(output, SpecialAttackMarker::Commit);
        } else {
            return output;
        }
    }
    if (state.phase == SpecialAttackPhase::Active) {
        output.bullets = bulletpattern::advance(definition.pattern, state.pattern,
                                                facing, liveAim);
        if (++state.phaseTick >= definition.activeTicks) {
            state.phase = SpecialAttackPhase::Recovery;
            state.phaseTick = 0;
            bulletpattern::cancel(state.pattern);
            marker(output, SpecialAttackMarker::RecoveryStarted);
        }
        return output;
    }
    if (state.phase == SpecialAttackPhase::Recovery &&
        ++state.phaseTick >= definition.recoveryTicks) {
        state.phase = SpecialAttackPhase::Finished;
        state.phaseTick = 0;
        marker(output, SpecialAttackMarker::Finished);
    }
    return output;
}

SpecialAttackStep cancel(SpecialAttackState& state)
{
    SpecialAttackStep output;
    if (state.phase != SpecialAttackPhase::Idle &&
        state.phase != SpecialAttackPhase::Finished)
        marker(output, SpecialAttackMarker::Cancelled);
    state = {};
    return output;
}

} // namespace specialattack
} // namespace game
