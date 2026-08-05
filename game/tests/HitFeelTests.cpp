// Screen shake and hit-stop: the two strongest feedback channels, and the two
// easiest to get subtly wrong in ways that only show up as "the game feels
// bad". Every property pinned here is one the skill's pitfall list names.

#include "HitFeel.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "HitFeelTests: " << message << '\n';
        std::exit(1);
    }
}

// Runs the feel for `seconds` of wall time at 60 Hz.
void run(game::HitFeel& feel, float seconds)
{
    const float dt = 1.0f / 60.0f;
    for (float t = 0.0f; t < seconds; t += dt)
        feel.update(dt);
}

float maxAbs(glm::vec3 v)
{
    return std::max(std::abs(v.x), std::max(std::abs(v.y), std::abs(v.z)));
}

void shakeDecaysToRest()
{
    // Juice is transient. A shake that never returns to zero becomes the new
    // resting state and stops reading as feedback at all.
    game::HitFeel feel;
    feel.impact(game::ImpactTier::Kill);
    require(feel.trauma() > 0.0f, "a kill should produce trauma");
    run(feel, 3.0f);
    require(feel.trauma() == 0.0f, "trauma must decay to exactly zero");
    require(maxAbs(feel.shakeOffset()) == 0.0f,
            "the offset must return to rest, not to something small");
    require(maxAbs(feel.shakeRotationDegrees()) == 0.0f,
            "and so must the rotation");
}

void shakeIsQuadraticInTrauma()
{
    // The whole point of a trauma model: half the trauma is a QUARTER of the
    // shake, so a graze barely moves the view and a kill punches. Linear would
    // make every hit feel the same size.
    game::HitFeel low;
    low.addTrauma(0.5f);
    low.update(1.0f / 600.0f);
    game::HitFeel high;
    high.addTrauma(1.0f);
    high.update(1.0f / 600.0f);

    const float lowPeak = maxAbs(low.shakeOffset());
    const float highPeak = maxAbs(high.shakeOffset());
    require(lowPeak > 0.0f && highPeak > 0.0f, "both should shake");
    // 0.5^2 / 1.0^2 = 0.25. Generous window: the sine phase is shared, so the
    // ratio is exact, but the guard is against the curve changing shape.
    const float ratio = lowPeak / highPeak;
    require(ratio > 0.20f && ratio < 0.30f,
            "half the trauma must give about a quarter of the shake");
}

void traumaAddsAndClamps()
{
    // Two hits in one frame are one bigger hit. Replacing would let a graze
    // landing just after a kill cut the kill's shake short.
    game::HitFeel feel;
    feel.addTrauma(0.3f);
    feel.addTrauma(0.3f);
    require(std::abs(feel.trauma() - 0.6f) < 1e-5f, "trauma should add");
    feel.addTrauma(10.0f);
    require(feel.trauma() <= 1.0f, "and clamp at one");
}

void hitStopEndsItself()
{
    // The pitfall that hangs a game: a stop driven by the clock it is slowing
    // never elapses. This is fed the WALL delta, so it always ends.
    game::HitFeel feel;
    feel.impact(game::ImpactTier::Kill);
    require(feel.stopping(), "a kill should stop time");
    require(feel.clockScale() < 1.0f, "and slow the clock while it does");
    run(feel, 1.0f);
    require(!feel.stopping(), "the stop must end on its own");
    require(feel.clockScale() == 1.0f, "and hand the clock back at 1");
}

void hitStopsDoNotStack()
{
    // Automatic fire landing four hits must not freeze the game for four stops
    // back to back. The longest request wins instead.
    game::HitFeel feel;
    for (int i = 0; i < 8; ++i)
        feel.impact(game::ImpactTier::Solid);
    const float solid = game::HitFeelTuning{}.stopSeconds[1];
    run(feel, solid + 0.02f);
    require(!feel.stopping(),
            "eight solid hits must not queue eight stops -- the longest wins");
}

void firingNeverStopsTime()
{
    // Hit-stop on a routine action is the fastest way to make a game feel
    // broken, and firing is the most routine action there is.
    game::HitFeel feel;
    feel.impact(game::ImpactTier::Light);
    require(!feel.stopping(), "the light tier must carry no hit-stop");
    require(feel.trauma() > 0.0f, "but it should still nudge the camera");
}

void sustainedHitsDoNotAccumulate()
{
    // The bug this test exists for: the light tier was fired once per shot, and
    // the talon shoots every 0.09s. Trauma was being ADDED faster than it
    // decayed, so holding the trigger pinned the camera near full shake and it
    // never settled -- which reads as a broken camera, not a powerful gun.
    //
    // Firing no longer feeds the camera at all (the viewmodel kick is the
    // feedback), but the tier still exists for wall impacts, so the budget is
    // pinned here: one tier's trauma must decay away faster than a fast weapon
    // can re-add it.
    const game::HitFeelTuning t;
    const float fastestFireInterval = 0.09f; // riven_talon, weapons.toml

    // Every tier a *repeatable* action can raise has to lose trauma faster
    // than that action can add it. Light is a wall impact and Solid is a
    // landed hit, and an automatic weapon produces either eleven times a
    // second. Heavy and Kill are not repeatable at that rate -- you only kill
    // a thing once -- so they are exempt and carry the big numbers.
    for (int tier = 0; tier <= int(game::ImpactTier::Solid); ++tier) {
        require(t.trauma[tier] / fastestFireInterval < t.traumaDecay,
                "a repeatable tier must decay faster than the fastest weapon "
                "can re-add it, or sustained fire pins the camera at full "
                "shake");
    }

    // And demonstrated, not just arithmetic: stream landed hits at the talon's
    // rate for three seconds and see where the view settles.
    game::HitFeel feel;
    const float dt = 1.0f / 60.0f;
    float sinceShot = 0.0f;
    float peak = 0.0f;
    for (float elapsed = 0.0f; elapsed < 3.0f; elapsed += dt) {
        sinceShot += dt;
        if (sinceShot >= fastestFireInterval) {
            sinceShot = 0.0f;
            feel.impact(game::ImpactTier::Solid);
        }
        feel.update(dt);
        peak = std::max(peak, feel.trauma());
    }
    require(peak < 0.45f,
            "sustained landed hits must settle into a rumble, not saturate");
    // And a kill landing on top of that stream must still stand out from it.
    require(t.trauma[int(game::ImpactTier::Kill)] > peak,
            "a kill has to read louder than the rumble it lands in");
}

void tiersAreOrdered()
{
    // The tier table is what keeps the whole game proportional; if a kill ever
    // shook less than a graze, every call site would start passing numbers.
    const game::HitFeelTuning t;
    for (int i = 1; i < game::kImpactTierCount; ++i) {
        require(t.trauma[i] > t.trauma[i - 1],
                "each tier must shake more than the one below it");
        require(t.stopSeconds[i] >= t.stopSeconds[i - 1],
                "and stop time for at least as long");
    }
}

void accessibilityScalesSwitchChannelsOff()
{
    // Not merely "reduce": zero has to mean off. People turn these down for
    // real reasons and a residual shake is still a shake.
    game::HitFeelTuning quiet;
    quiet.shakeScale = 0.0f;
    quiet.hitStopScale = 0.0f;
    game::HitFeel feel;
    feel.setTuning(quiet);
    feel.impact(game::ImpactTier::Kill);
    feel.update(1.0f / 60.0f);
    require(feel.trauma() == 0.0f, "shake_scale 0 must produce no trauma");
    require(!feel.stopping(), "hit_stop_scale 0 must produce no stop");
    require(feel.clockScale() == 1.0f, "and must never touch the clock");
}

void resetClearsEverything()
{
    game::HitFeel feel;
    feel.impact(game::ImpactTier::Kill);
    feel.reset();
    require(feel.trauma() == 0.0f && !feel.stopping(),
            "a level transition must not carry a shake into the next level");
    require(feel.clockScale() == 1.0f, "or leave the clock slowed");
}

void badTuningIsRejected()
{
    require(validHitFeelTuning(game::HitFeelTuning{}),
            "the shipped defaults are valid");

    game::HitFeelTuning noDecay;
    noDecay.traumaDecay = 0.0f;
    require(!validHitFeelTuning(noDecay),
            "a shake that never decays is a new resting state, not feedback");

    game::HitFeelTuning speedUp;
    speedUp.stopTimeScale = 1.5f;
    require(!validHitFeelTuning(speedUp),
            "a 'stop' above 1 would speed the game up on a hit");

    game::HitFeelTuning endless;
    endless.stopSeconds[3] = 2.0f;
    require(!validHitFeelTuning(endless),
            "a two-second freeze is a stall, not a punch");
}

void tomlRoundTrips()
{
    game::HitFeelTuning tuning;
    require(game::loadHitFeelTuning(R"(
[feel]
shake_scale = 0.5
hit_stop_time_scale = 0.2
[feel.kill]
trauma = 0.9
hit_stop = 0.2
)",
                                    tuning),
            "the section parses");
    require(std::abs(tuning.shakeScale - 0.5f) < 1e-5f, "the master scale");
    require(std::abs(tuning.stopTimeScale - 0.2f) < 1e-5f, "the stop scale");
    require(std::abs(tuning.trauma[3] - 0.9f) < 1e-5f, "the kill tier");

    // A file with no [feel] keeps the shipped numbers rather than zeroing them.
    game::HitFeelTuning untouched;
    require(game::loadHitFeelTuning("[player]\nmove_speed = 8.0\n", untouched),
            "a document without the section is not an error");
    require(untouched.shakeScale == game::HitFeelTuning{}.shakeScale,
            "and changes nothing");

    // A rejected table leaves the caller alone rather than half-applying.
    game::HitFeelTuning before;
    game::HitFeelTuning after = before;
    require(!game::loadHitFeelTuning("[feel]\ntrauma_decay = 0.0\n", after),
            "an invalid decay is rejected");
    require(after.traumaDecay == before.traumaDecay,
            "a rejected table must not have been half-applied");
}

} // namespace

int main()
{
    shakeDecaysToRest();
    shakeIsQuadraticInTrauma();
    traumaAddsAndClamps();
    hitStopEndsItself();
    hitStopsDoNotStack();
    firingNeverStopsTime();
    sustainedHitsDoNotAccumulate();
    tiersAreOrdered();
    accessibilityScalesSwitchChannelsOff();
    resetClearsEverything();
    badTuningIsRejected();
    tomlRoundTrips();
    std::cout << "HitFeelTests: ok\n";
    return 0;
}
