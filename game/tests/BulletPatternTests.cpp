#include "combat/BulletPattern.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "BulletPatternTests: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

game::BulletPatternTrack track(game::BulletPatternShape shape, int count)
{
    game::BulletPatternTrack result;
    result.volley.projectileId = "test_bolt";
    result.volley.shape = shape;
    result.volley.shotCount = std::uint16_t(count);
    result.volley.arcRadians = glm::radians(60.0f);
    return result;
}

} // namespace

int main()
{
    using namespace game;

    require(bulletpattern::secondsToTicks(0.08f, 1.0f / 60.0f) == 5,
            "seconds were not conservatively quantized to ticks");

    BulletPatternDef fan;
    fan.id = "fan";
    fan.tracks.push_back(track(BulletPatternShape::Fan, 5));
    std::string error;
    require(bulletpattern::validate(fan, 10, error),
            "valid fan failed validation");
    BulletPatternState fanState;
    bulletpattern::begin(fanState, {0, 0, 1});
    const BulletPatternStep fanStep =
        bulletpattern::advance(fan, fanState, {0, 0, 1}, {0, 0, 1});
    require(fanStep.commandCount == 5, "fan emitted wrong shot count");
    require(fanStep.commands[0].direction.x *
                    fanStep.commands[4].direction.x <
                0.0f,
            "fan was not symmetric");

    BulletPatternDef ring;
    ring.id = "ring";
    ring.tracks.push_back(track(BulletPatternShape::Ring, 8));
    require(bulletpattern::validate(ring, 10, error),
            "valid ring failed validation");
    BulletPatternState ringState;
    bulletpattern::begin(ringState, {0, 0, 1});
    const BulletPatternStep ringStep =
        bulletpattern::advance(ring, ringState, {0, 0, 1}, {0, 0, 1});
    require(ringStep.commandCount == 8, "ring emitted wrong shot count");
    require(glm::dot(ringStep.commands[0].direction,
                     ringStep.commands[4].direction) < -0.99f,
            "ring did not contain opposite lanes");

    BulletPatternDef spiral;
    spiral.id = "spiral";
    BulletPatternTrack spiralTrack = track(BulletPatternShape::Aimed, 1);
    spiralTrack.repetitions = 3;
    spiralTrack.intervalTicks = 1;
    spiralTrack.rotationPerRepeatRadians = glm::radians(30.0f);
    spiral.tracks.push_back(spiralTrack);
    require(bulletpattern::validate(spiral, 3, error),
            "valid spiral failed validation");
    BulletPatternState spiralState;
    bulletpattern::begin(spiralState, {0, 0, 1});
    const auto spiral0 = bulletpattern::advance(
        spiral, spiralState, {0, 0, 1}, {0, 0, 1});
    const auto spiral1 = bulletpattern::advance(
        spiral, spiralState, {0, 0, 1}, {0, 0, 1});
    require(spiral0.commandCount == 1 && spiral1.commandCount == 1 &&
                glm::dot(spiral0.commands[0].direction,
                         spiral1.commands[0].direction) < 0.99f,
            "rotation per repeat did not produce a spiral");

    SpecialAttackDef special;
    special.id = "rite";
    special.telegraphTicks = 2;
    special.activeTicks = 3;
    special.recoveryTicks = 2;
    special.pattern = spiral;
    require(specialattack::validate(special, error),
            "valid special attack failed validation");
    SpecialAttackState specialState;
    const auto started = specialattack::begin(special, specialState, {0, 0, 1});
    require(started.markerCount == 1 &&
                started.markers[0] == SpecialAttackMarker::TelegraphStarted,
            "special attack omitted telegraph marker");
    specialattack::advance(special, specialState, {0, 0, 1}, {0, 0, 1});
    const auto committed = specialattack::advance(
        special, specialState, {0, 0, 1}, {0, 0, 1});
    require(committed.markerCount >= 1 &&
                committed.markers[0] == SpecialAttackMarker::Commit &&
                committed.bullets.commandCount == 1,
            "commit marker and tick-zero volley were not synchronized");

    BulletPatternDef invalid = fan;
    invalid.tracks[0].repetitions = 2;
    invalid.tracks[0].intervalTicks = 0;
    require(!bulletpattern::validate(invalid, 10, error),
            "unbounded same-tick repeat was accepted");
    invalid = fan;
    invalid.tracks[0].volley.arcRadians =
        std::numeric_limits<float>::quiet_NaN();
    require(!bulletpattern::validate(invalid, 10, error),
            "non-finite pattern angle was accepted");

    std::cout << "BulletPatternTests OK\n";
    return EXIT_SUCCESS;
}
