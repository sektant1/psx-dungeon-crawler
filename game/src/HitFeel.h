#pragma once

#include <glm/glm.hpp>

#include <string>

namespace game {

// How much an event is worth. One dial instead of a number at every call site:
// a graze and a killing blow differ by which word the caller passes, so the
// whole game's feedback stays proportional and can be retuned in one table.
//
// Ordered by weight, and the order is load-bearing -- the tuning arrays below
// are indexed by it.
enum class ImpactTier {
    Light, // a shot leaving the barrel, a projectile hitting a wall
    Solid, // a landed hit
    Heavy, // a hit that staggers, or one the player takes
    Kill,  // something died
};
inline constexpr int kImpactTierCount = 4;

// Screen shake and hit-stop, the two strongest and most abusable feedback
// channels, in one place so they cannot drift apart.
//
// Both were missing entirely. A landed hit already threw particles, blood, a
// poise chip, a reaction and a grunt -- five channels -- but nothing moved the
// camera and nothing interrupted time, which are the two that read as *impact*
// rather than as decoration.
struct HitFeelTuning {
    // Accessibility masters, both authored in game.toml and on the debug panel.
    // Zero switches the channel off outright rather than merely reducing it,
    // because "reduce screen shake" is a setting people need for real reasons.
    float shakeScale = 1.0f;
    float hitStopScale = 1.0f;

    // Trauma decays linearly; the *shake* is trauma squared, so a small hit
    // barely moves the view and a big one punches. That curve is the whole
    // reason this is a trauma model rather than a per-event offset.
    // Fast enough that a weapon landing a hit every 0.09s (the talon) cannot
    // out-add it: sustained fire settles at a faint rumble instead of pinning
    // the view at full shake. See HitFeelTests::sustainedHitsDoNotAccumulate.
    float traumaDecay = 3.0f; // trauma per second
    // Metres, at full shake. Deliberately small: the viewmodel moves loudly and
    // the camera moves subtly (docs/fps-viewmodel.md), and a first-person
    // camera that translates far reads as nausea rather than as force.
    glm::vec3 maxOffset{0.045f, 0.036f, 0.022f};
    float maxPitchDegrees = 0.75f;
    float maxRollDegrees = 1.10f;
    // Hz of the underlying oscillation. Three incommensurate multiples of this
    // drive the three axes, so the motion never resolves into a circle.
    float frequency = 22.0f;

    // What the game clock runs at while stopped. Not zero: a fully frozen frame
    // reads as a hitch, where a very slow one reads as weight.
    float stopTimeScale = 0.08f;

    // Per tier, indexed by ImpactTier.
    // The quadratic curve does the separating: solid is a faint tap, a kill is
    // a real punch. Solid is deliberately below decay*0.09 so a stream of
    // landed hits rumbles rather than saturates.
    float trauma[kImpactTierCount] = {0.10f, 0.20f, 0.45f, 0.75f};
    // Light gets none on purpose: hit-stop on a routine action is the fastest
    // way to make a game feel broken, and firing is a routine action.
    float stopSeconds[kImpactTierCount] = {0.0f, 0.035f, 0.060f, 0.110f};
};

// Pure math: no renderer, no ECS, no clock of its own. That is what makes the
// decay curve, the tier table and the hit-stop lifetime testable without a GPU
// (game/tests/HitFeelTests.cpp), and what keeps the shake off the simulation --
// the caller reads shakeOffset() at presentation time and never feeds it back.
class HitFeel {
public:
    void setTuning(const HitFeelTuning& tuning) { mTuning = tuning; }
    const HitFeelTuning& tuning() const { return mTuning; }
    HitFeelTuning& tuning() { return mTuning; }

    // The one call a gameplay event makes.
    void impact(ImpactTier tier);

    // Trauma ADDS and clamps rather than replacing: two hits in one frame
    // should be felt as one bigger hit, and replacing would let a graze
    // arriving just after a kill cut the kill's shake short.
    void addTrauma(float amount);
    // The longest request wins for the same reason a stack would be wrong:
    // automatic fire landing four hits must not freeze the game for four
    // stops back to back.
    void requestHitStop(float seconds);

    // `realDt` MUST be the unscaled wall delta. Driving this from the game
    // clock would make a hit-stop unable to end itself -- the thing it is
    // slowing is the clock it would be reading.
    void update(float realDt);

    // Presentation-only, recomputed every frame, never integrated back into
    // anything the simulation reads.
    glm::vec3 shakeOffset() const { return mOffset; }
    glm::vec3 shakeRotationDegrees() const { return mRotation; }

    float trauma() const { return mTrauma; }
    bool stopping() const { return mStopRemaining > 0.0f; }
    // Hand to eng::Clock::setScale every frame. 1 when nothing is happening.
    float clockScale() const;

    // Level transition, respawn, teleport: a shake that survives one is a
    // camera shaking for something the player can no longer see.
    void reset();

private:
    HitFeelTuning mTuning{};
    float mTrauma = 0.0f;
    float mPhase = 0.0f;
    float mStopRemaining = 0.0f;
    glm::vec3 mOffset{0.0f};
    glm::vec3 mRotation{0.0f};
};

// Every field finite, non-negative where that is the only sensible sign.
bool validHitFeelTuning(const HitFeelTuning& tuning);

// `[feel]` out of a TOML document. A missing section keeps the shipped
// defaults; a rejected one leaves the caller's tuning untouched rather than
// half-applied.
bool loadHitFeelTuning(const char* tomlSource, HitFeelTuning& out);
bool loadHitFeelTuningFile(const std::string& tomlPath, HitFeelTuning& out);

} // namespace game
