#include "HitFeel.h"

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>

namespace game {
namespace {

int tierIndex(ImpactTier tier)
{
    const int index = int(tier);
    return std::clamp(index, 0, kImpactTierCount - 1);
}

bool finite(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

void HitFeel::impact(ImpactTier tier)
{
    const int index = tierIndex(tier);
    addTrauma(mTuning.trauma[index]);
    requestHitStop(mTuning.stopSeconds[index]);
}

void HitFeel::addTrauma(float amount)
{
    if (!std::isfinite(amount) || amount <= 0.0f)
        return;
    mTrauma = std::clamp(mTrauma + amount * std::max(mTuning.shakeScale, 0.0f),
                         0.0f, 1.0f);
}

void HitFeel::requestHitStop(float seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0f)
        return;
    mStopRemaining =
        std::max(mStopRemaining, seconds * std::max(mTuning.hitStopScale, 0.0f));
}

void HitFeel::update(float realDt)
{
    if (!std::isfinite(realDt) || realDt < 0.0f)
        realDt = 0.0f;

    mStopRemaining = std::max(0.0f, mStopRemaining - realDt);

    if (mTrauma <= 0.0f) {
        mTrauma = 0.0f;
        mOffset = glm::vec3(0.0f);
        mRotation = glm::vec3(0.0f);
        // Phase is deliberately NOT reset: restarting it at zero would make
        // every shake begin with the same flick in the same direction, which
        // reads as a scripted nudge rather than as an impact.
        return;
    }

    mTrauma = std::max(0.0f, mTrauma - mTuning.traumaDecay * realDt);
    // The shake continues on the wall clock through a hit-stop, which is what
    // makes the stop read as impact rather than as a dropped frame.
    mPhase += realDt * mTuning.frequency;

    // Quadratic: gentle at low trauma, sharp at high. Sampled from sines at
    // incommensurate multiples rather than a fresh random per frame -- random
    // per frame buzzes like static and never settles.
    const float shake = mTrauma * mTrauma;
    mOffset = glm::vec3(mTuning.maxOffset.x * shake * std::sin(mPhase * 1.00f),
                        mTuning.maxOffset.y * shake * std::sin(mPhase * 1.63f),
                        mTuning.maxOffset.z * shake * std::sin(mPhase * 2.17f));
    mRotation =
        glm::vec3(mTuning.maxPitchDegrees * shake * std::sin(mPhase * 1.31f),
                  0.0f,
                  mTuning.maxRollDegrees * shake * std::sin(mPhase * 0.87f));
}

float HitFeel::clockScale() const
{
    return stopping() ? std::max(mTuning.stopTimeScale, 0.0f) : 1.0f;
}

void HitFeel::reset()
{
    mTrauma = 0.0f;
    mStopRemaining = 0.0f;
    mOffset = glm::vec3(0.0f);
    mRotation = glm::vec3(0.0f);
}

bool validHitFeelTuning(const HitFeelTuning& t)
{
    if (!finite(t.maxOffset) || !std::isfinite(t.traumaDecay) ||
        !std::isfinite(t.frequency) || !std::isfinite(t.stopTimeScale) ||
        !std::isfinite(t.maxPitchDegrees) || !std::isfinite(t.maxRollDegrees) ||
        !std::isfinite(t.shakeScale) || !std::isfinite(t.hitStopScale))
        return false;
    if (t.traumaDecay <= 0.0f || t.frequency <= 0.0f)
        return false; // a shake that never decays is a new resting state
    if (t.shakeScale < 0.0f || t.hitStopScale < 0.0f)
        return false;
    if (t.stopTimeScale < 0.0f || t.stopTimeScale > 1.0f)
        return false; // above 1 would speed the game up on a hit
    for (int i = 0; i < kImpactTierCount; ++i) {
        if (!std::isfinite(t.trauma[i]) || t.trauma[i] < 0.0f ||
            !std::isfinite(t.stopSeconds[i]) || t.stopSeconds[i] < 0.0f)
            return false;
        // A quarter second of frozen time is already long enough to feel like
        // a stall rather than a punch.
        if (t.stopSeconds[i] > 0.25f)
            return false;
    }
    return true;
}

namespace {
bool applyTable(const toml::table& root, HitFeelTuning& out);
}

bool loadHitFeelTuning(const char* tomlSource, HitFeelTuning& out)
{
    const toml::parse_result result = toml::parse(tomlSource);
    if (!result)
        return false;
    return applyTable(result.table(), out);
}

bool loadHitFeelTuningFile(const std::string& tomlPath, HitFeelTuning& out)
{
    const toml::parse_result result = toml::parse_file(tomlPath);
    if (!result)
        return false;
    return applyTable(result.table(), out);
}

namespace {
bool applyTable(const toml::table& root, HitFeelTuning& out)
{
    const toml::table* feel = root["feel"].as_table();
    if (!feel)
        return true; // no section is not an error: the default is the ship

    HitFeelTuning parsed = out;
    const auto number = [&](const char* key, float fallback) {
        return float((*feel)[key].value_or(double(fallback)));
    };
    parsed.shakeScale = number("shake_scale", parsed.shakeScale);
    parsed.hitStopScale = number("hit_stop_scale", parsed.hitStopScale);
    parsed.traumaDecay = number("trauma_decay", parsed.traumaDecay);
    parsed.frequency = number("shake_frequency", parsed.frequency);
    parsed.maxPitchDegrees = number("shake_pitch_degrees", parsed.maxPitchDegrees);
    parsed.maxRollDegrees = number("shake_roll_degrees", parsed.maxRollDegrees);
    parsed.stopTimeScale = number("hit_stop_time_scale", parsed.stopTimeScale);
    if (const toml::array* offset = (*feel)["shake_offset"].as_array();
        offset && offset->size() == 3) {
        parsed.maxOffset = {float((*offset)[0].value_or(0.0)),
                            float((*offset)[1].value_or(0.0)),
                            float((*offset)[2].value_or(0.0))};
    }

    static const char* kTierKeys[kImpactTierCount] = {"light", "solid", "heavy",
                                                      "kill"};
    for (int i = 0; i < kImpactTierCount; ++i) {
        if (const toml::table* tier = (*feel)[kTierKeys[i]].as_table()) {
            parsed.trauma[i] =
                float((*tier)["trauma"].value_or(double(parsed.trauma[i])));
            parsed.stopSeconds[i] = float(
                (*tier)["hit_stop"].value_or(double(parsed.stopSeconds[i])));
        }
    }

    if (!validHitFeelTuning(parsed))
        return false;
    out = parsed;
    return true;
}
} // namespace

} // namespace game
