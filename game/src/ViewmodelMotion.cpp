// ViewmodelRig.cpp — camera-space placement and procedural motion for the
// shared first-person rig. Presentation only: nothing here decides whether a
// shot happened, only how the hands react to one.

#include "ViewmodelMotion.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string_view>
#include <system_error>
#include <vector>

namespace game {
namespace {

constexpr float kTwoPi = 6.283185307179586f;

float number(const toml::table& table, const char* key, float fallback)
{
    return float(table[key].value_or(double(fallback)));
}

glm::vec3 vector3(const toml::table& table, const char* key, glm::vec3 fallback)
{
    const toml::array* values = table[key].as_array();
    if (!values || values->size() != 3)
        return fallback;
    return {float((*values)[0].value_or(double(fallback.x))),
            float((*values)[1].value_or(double(fallback.y))),
            float((*values)[2].value_or(double(fallback.z)))};
}

bool finite(std::initializer_list<float> values)
{
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

bool finite(glm::vec3 value)
{
    return finite({value.x, value.y, value.z});
}

// Frame-rate independent approach toward zero. Used by every spring-ish layer
// here (recoil, landing, sway return) so they all settle the same way.
float decay(float value, float rate, float dt)
{
    if (rate <= 0.0f)
        return value;
    return value * std::exp(-rate * dt);
}

float wrapPhase(float phase)
{
    return phase - kTwoPi * std::floor(phase / kTwoPi);
}

} // namespace

ViewmodelFeel viewmodelFeel(const WeaponViewmodelDef& definition)
{
    ViewmodelFeel feel;
    feel.offset = definition.handsOffset;
    feel.rotationDegrees = definition.handsRotationDegrees;
    feel.scale = definition.handsScale;
    feel.recoilDistance = definition.recoilDistance;
    feel.recoilPitchDegrees = definition.recoilPitchDegrees;
    feel.recoilYawDegrees = definition.recoilYawDegrees;
    feel.recoilRecovery = definition.recoilRecovery;
    feel.movementBob = definition.movementBob;
    feel.movementBobSpeed = definition.movementBobSpeed;
    feel.idleSway = definition.idleSway;
    feel.lookSway = definition.lookSway;
    return feel;
}

void ViewmodelMotion::kick(float strength)
{
    if (!std::isfinite(strength))
        return;
    // Saturating rather than accumulating: an automatic weapon fires faster
    // than the spring settles, and an unbounded sum walks the hands off screen.
    mRecoil = std::clamp(mRecoil + std::max(0.0f, strength), 0.0f, 1.35f);
}

void ViewmodelMotion::reset()
{
    mBobPhase = 0.0f;
    mIdlePhase = 0.0f;
    mRecoil = 0.0f;
    mLanding = 0.0f;
    mSway = glm::vec2(0.0f);
    mWasGrounded = true;
}

ViewmodelPose ViewmodelMotion::update(const ViewmodelMotionInput& input)
{
    const ViewmodelRig& t = mTuning;

    ViewmodelPose pose;
    pose.position = t.offset + mFeel.offset;
    pose.rotationDegrees = t.rotation + mFeel.rotationDegrees;
    pose.scale = t.scale * mFeel.scale;

    const float dt =
        std::isfinite(input.dt) ? std::clamp(input.dt, 0.0f, 0.1f) : 0.0f;
    if (!t.motionEnabled) {
        // Authoring pose: hold the socket exactly, and let the accumulators
        // relax so unfreezing does not snap.
        mRecoil = mLanding = 0.0f;
        mSway = glm::vec2(0.0f);
        mWasGrounded = input.grounded;
        return pose;
    }

    const float speed =
        std::isfinite(input.horizontalSpeed) ? std::max(0.0f, input.horizontalSpeed) : 0.0f;
    const float reference = t.bobReferenceSpeed > 0.01f ? t.bobReferenceSpeed : 6.0f;
    // Above the reference speed the bob keeps growing a little (sprinting has
    // to read differently from running) but not without limit.
    const float speedFactor = std::min(speed / reference, 1.4f);
    const float airborne = input.grounded ? 0.0f : 1.0f;

    // ---- movement bob -----------------------------------------------------
    // Figure-eight: one horizontal cycle per step pair, two vertical ones, so
    // the hands drop on each footfall instead of once per stride.
    if (speedFactor > 0.001f && airborne == 0.0f) {
        mBobPhase = wrapPhase(mBobPhase +
                              dt * mFeel.movementBobSpeed * (0.6f + speedFactor));
        const float amount = mFeel.movementBob * t.bobScale * speedFactor;
        pose.position.x += std::sin(mBobPhase) * amount;
        pose.position.y -= std::abs(std::sin(mBobPhase * 2.0f)) * amount * 0.75f;
        pose.rotationDegrees.z +=
            std::sin(mBobPhase) * t.bobRollDegrees * t.bobScale * speedFactor;
    }
    else {
        // Ease the phase back to the bottom of the cycle so the next step
        // starts from rest rather than wherever the player stopped.
        mBobPhase = wrapPhase(mBobPhase + dt * 2.0f);
    }

    // ---- idle sway --------------------------------------------------------
    // Breathing, faded out by movement: bob and breath together read as noise.
    mIdlePhase = wrapPhase(mIdlePhase + dt * 1.35f);
    const float idleWeight = std::max(0.0f, 1.0f - speedFactor);
    const float idle = mFeel.idleSway * t.swayScale * idleWeight;
    pose.position.x += std::sin(mIdlePhase * 0.7f) * idle;
    pose.position.y += std::sin(mIdlePhase) * idle * 0.6f;

    // ---- mouse-look sway --------------------------------------------------
    // The hands lag the view and are pulled back to centre. Clamped, because a
    // fast flick would otherwise throw them out of frame for a frame or two.
    const glm::vec2 look = glm::vec2(
        std::isfinite(input.lookDelta.x) ? input.lookDelta.x : 0.0f,
        std::isfinite(input.lookDelta.y) ? input.lookDelta.y : 0.0f);
    mSway -= look * mFeel.lookSway * t.swayScale;
    mSway = glm::vec2(std::clamp(mSway.x, -t.swayMax, t.swayMax),
                      std::clamp(mSway.y, -t.swayMax, t.swayMax));
    mSway.x = decay(mSway.x, t.swayReturn, dt);
    mSway.y = decay(mSway.y, t.swayReturn, dt);
    pose.position.x += mSway.x;
    pose.position.y += mSway.y;
    if (t.swayMax > 0.0001f) {
        // Rotate into the lag as well: pure translation reads as sliding.
        pose.rotationDegrees.y += (mSway.x / t.swayMax) * t.swayRollDegrees;
        pose.rotationDegrees.x -= (mSway.y / t.swayMax) * t.swayRollDegrees * 0.5f;
    }

    // ---- firing recoil ----------------------------------------------------
    // Camera space forward is -z, so the kick pushes +z: toward the eye.
    if (mRecoil > 0.0001f) {
        const float amount = mRecoil * t.recoilScale;
        pose.position.z += mFeel.recoilDistance * amount;
        pose.rotationDegrees.x += mFeel.recoilPitchDegrees * amount;
        pose.rotationDegrees.y += mFeel.recoilYawDegrees * amount;
    }
    mRecoil = decay(mRecoil, mFeel.recoilRecovery, dt);

    // ---- landing impulse --------------------------------------------------
    if (input.grounded && !mWasGrounded)
        mLanding = 1.0f;
    mWasGrounded = input.grounded;
    if (mLanding > 0.0001f)
        pose.position.y -= t.landingDip * mLanding;
    mLanding = decay(mLanding, t.landingRecovery, dt);

    return pose;
}

bool validViewmodelRig(const ViewmodelRig& t)
{
    return finite(t.offset) && finite(t.rotation) &&
           finite({t.scale, t.bobScale, t.swayScale, t.recoilScale,
                   t.bobReferenceSpeed, t.bobRollDegrees, t.swayReturn,
                   t.swayMax, t.swayRollDegrees, t.landingDip,
                   t.landingRecovery}) &&
           t.scale > 0.0f && t.bobScale >= 0.0f && t.swayScale >= 0.0f &&
           t.recoilScale >= 0.0f && t.bobReferenceSpeed > 0.0f &&
           t.swayReturn >= 0.0f && t.swayMax >= 0.0f && t.landingDip >= 0.0f &&
           t.landingRecovery >= 0.0f;
}

namespace {

bool parseTuning(const toml::table& root, ViewmodelRig& out)
{
    const toml::table* table = root["player_viewmodel"].as_table();
    if (!table)
        return true; // no section authored: shipped framing stands

    ViewmodelRig parsed = out;
    parsed.offset = vector3(*table, "offset", parsed.offset);
    parsed.rotation =
        vector3(*table, "rotation", parsed.rotation);
    parsed.scale = number(*table, "scale", parsed.scale);
    parsed.bobScale = number(*table, "bob_scale", parsed.bobScale);
    parsed.swayScale = number(*table, "sway_scale", parsed.swayScale);
    parsed.recoilScale = number(*table, "recoil_scale", parsed.recoilScale);
    parsed.bobReferenceSpeed =
        number(*table, "bob_reference_speed", parsed.bobReferenceSpeed);
    parsed.bobRollDegrees =
        number(*table, "bob_roll_degrees", parsed.bobRollDegrees);
    parsed.swayReturn = number(*table, "sway_return", parsed.swayReturn);
    parsed.swayMax = number(*table, "sway_max", parsed.swayMax);
    parsed.swayRollDegrees =
        number(*table, "sway_roll_degrees", parsed.swayRollDegrees);
    parsed.landingDip = number(*table, "landing_dip", parsed.landingDip);
    parsed.landingRecovery =
        number(*table, "landing_recovery", parsed.landingRecovery);
    parsed.motionEnabled =
        (*table)["motion_enabled"].value_or(parsed.motionEnabled);
    if (!validViewmodelRig(parsed))
        return false;
    out = parsed;
    return true;
}

} // namespace

bool loadViewmodelRig(const std::string& tomlPath,
                            ViewmodelRig& out)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed || !parseTuning(parsed.table(), out)) {
        eng::log::error("Viewmodel rig: invalid [player_viewmodel] in '%s'",
                        tomlPath.c_str());
        return false;
    }
    return true;
}

bool parseViewmodelRig(const char* tomlSource, ViewmodelRig& out)
{
    toml::parse_result parsed = toml::parse(tomlSource);
    return parsed && parseTuning(parsed.table(), out);
}

namespace {

// The section's keys, in the order a fresh section is written. One table, so
// the loader, the paste-me formatter and the in-place saver cannot disagree
// about what a rig is made of.
struct RigKey {
    const char* name;
    // Formats this key's current value. Two-argument rather than a member
    // pointer because three of them are vectors and one is a bool.
    std::string (*value)(const ViewmodelRig&);
};

std::string number3(float a, float b, float c, int decimals)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "[%.*f, %.*f, %.*f]", decimals,
                  double(a), decimals, double(b), decimals, double(c));
    return buffer;
}

std::string number1(float v, int decimals)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, double(v));
    return buffer;
}

const std::vector<RigKey>& rigKeys()
{
    static const std::vector<RigKey> kKeys = {
        {"offset", [](const ViewmodelRig& r) {
             return number3(r.offset.x, r.offset.y, r.offset.z, 4);
         }},
        {"rotation", [](const ViewmodelRig& r) {
             return number3(r.rotation.x, r.rotation.y, r.rotation.z, 2);
         }},
        {"scale", [](const ViewmodelRig& r) { return number1(r.scale, 4); }},
        {"bob_scale", [](const ViewmodelRig& r) { return number1(r.bobScale, 3); }},
        {"sway_scale", [](const ViewmodelRig& r) { return number1(r.swayScale, 3); }},
        {"recoil_scale",
         [](const ViewmodelRig& r) { return number1(r.recoilScale, 3); }},
        {"bob_reference_speed",
         [](const ViewmodelRig& r) { return number1(r.bobReferenceSpeed, 3); }},
        {"bob_roll_degrees",
         [](const ViewmodelRig& r) { return number1(r.bobRollDegrees, 3); }},
        {"sway_return", [](const ViewmodelRig& r) { return number1(r.swayReturn, 3); }},
        {"sway_max", [](const ViewmodelRig& r) { return number1(r.swayMax, 4); }},
        {"sway_roll_degrees",
         [](const ViewmodelRig& r) { return number1(r.swayRollDegrees, 3); }},
        {"landing_dip", [](const ViewmodelRig& r) { return number1(r.landingDip, 4); }},
        {"landing_recovery",
         [](const ViewmodelRig& r) { return number1(r.landingRecovery, 3); }},
        {"motion_enabled", [](const ViewmodelRig& r) {
             return std::string(r.motionEnabled ? "true" : "false");
         }},
    };
    return kKeys;
}

// The key a `key = value` line assigns, or empty for anything else. Leading
// whitespace and a commented-out line are both "not an assignment".
std::string_view assignedKey(std::string_view line)
{
    const std::size_t begin = line.find_first_not_of(" \t");
    if (begin == std::string_view::npos)
        return {};
    line.remove_prefix(begin);
    if (line.empty() || line.front() == '#' || line.front() == '[')
        return {};
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos)
        return {};
    std::string_view key = line.substr(0, equals);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
        key.remove_suffix(1);
    return key;
}

bool isSectionHeader(std::string_view line)
{
    const std::size_t begin = line.find_first_not_of(" \t");
    return begin != std::string_view::npos && line[begin] == '[';
}

} // namespace

bool saveViewmodelRig(const std::string& tomlPath, const ViewmodelRig& rig)
{
    if (!validViewmodelRig(rig)) {
        eng::log::error("Viewmodel rig: refusing to save an invalid tuning");
        return false;
    }

    std::vector<std::string> lines;
    {
        std::ifstream in(tomlPath);
        if (!in) {
            eng::log::error("Viewmodel rig: cannot read '%s'",
                            tomlPath.c_str());
            return false;
        }
        std::string line;
        while (std::getline(in, line))
            lines.push_back(std::move(line));
    }

    std::vector<std::string> out;
    out.reserve(lines.size() + rigKeys().size());
    std::vector<std::string> written;
    bool inSection = false;
    bool sawSection = false;

    // Appends whatever the section did not already carry. Called when the
    // section ends -- at the next header or at end of file -- so a key added to
    // the rig since this file was authored lands inside it rather than in
    // whatever section happens to follow.
    const auto flushMissing = [&] {
        for (const RigKey& key : rigKeys()) {
            if (std::find(written.begin(), written.end(), key.name) !=
                written.end())
                continue;
            out.push_back(std::string(key.name) + " = " + key.value(rig));
        }
    };

    for (const std::string& line : lines) {
        if (isSectionHeader(line)) {
            if (inSection)
                flushMissing();
            const std::size_t begin = line.find_first_not_of(" \t");
            inSection = line.compare(begin, 18, "[player_viewmodel]") == 0;
            sawSection = sawSection || inSection;
            out.push_back(line);
            continue;
        }
        if (!inSection) {
            out.push_back(line);
            continue;
        }
        const std::string_view key = assignedKey(line);
        const RigKey* known = nullptr;
        for (const RigKey& candidate : rigKeys())
            if (key == candidate.name)
                known = &candidate;
        if (!known) {
            out.push_back(line); // a comment, a blank, or a key not ours
            continue;
        }
        // The value only. Any trailing comment on the line is a note about the
        // key and survives the rewrite.
        std::string rewritten = std::string(key) + " = " + known->value(rig);
        const std::size_t hash = line.find('#', line.find('='));
        if (hash != std::string::npos)
            rewritten += "  " + line.substr(hash);
        out.push_back(std::move(rewritten));
        written.push_back(known->name);
    }
    if (inSection)
        flushMissing();
    if (!sawSection) {
        out.push_back("");
        out.push_back("[player_viewmodel]");
        for (const RigKey& key : rigKeys())
            out.push_back(std::string(key.name) + " = " + key.value(rig));
    }

    // Written beside the target and renamed over it: a crash mid-write must not
    // leave the game with a config it cannot parse on the next launch.
    const std::string temporary = tomlPath + ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) {
            eng::log::error("Viewmodel rig: cannot write '%s'",
                            temporary.c_str());
            return false;
        }
        for (const std::string& line : out)
            file << line << '\n';
        if (!file) {
            eng::log::error("Viewmodel rig: failed writing '%s'",
                            temporary.c_str());
            return false;
        }
    }
    std::error_code code;
    std::filesystem::rename(temporary, tomlPath, code);
    if (code) {
        std::filesystem::remove(temporary, code);
        eng::log::error("Viewmodel rig: cannot replace '%s'", tomlPath.c_str());
        return false;
    }
    eng::log::info("Viewmodel rig: saved [player_viewmodel] to '%s'",
                   tomlPath.c_str());
    return true;
}

std::string viewmodelRigToml(const ViewmodelRig& t)
{
    char buffer[1024];
    std::snprintf(
        buffer, sizeof(buffer),
        "[player_viewmodel]\n"
        "offset = [%.4f, %.4f, %.4f]\n"
        "rotation = [%.2f, %.2f, %.2f]\n"
        "scale = %.4f\n"
        "bob_scale = %.3f\n"
        "sway_scale = %.3f\n"
        "recoil_scale = %.3f\n"
        "bob_reference_speed = %.3f\n"
        "bob_roll_degrees = %.3f\n"
        "sway_return = %.3f\n"
        "sway_max = %.4f\n"
        "sway_roll_degrees = %.3f\n"
        "landing_dip = %.4f\n"
        "landing_recovery = %.3f\n"
        "motion_enabled = %s\n",
        t.offset.x, t.offset.y, t.offset.z, t.rotation.x,
        t.rotation.y, t.rotation.z, t.scale, t.bobScale,
        t.swayScale, t.recoilScale, t.bobReferenceSpeed, t.bobRollDegrees,
        t.swayReturn, t.swayMax, t.swayRollDegrees, t.landingDip,
        t.landingRecovery, t.motionEnabled ? "true" : "false");
    return buffer;
}

std::string viewmodelWeaponToml(const std::string& weaponId,
                                const WeaponViewmodelDef& v)
{
    char buffer[1024];
    std::snprintf(
        buffer, sizeof(buffer),
        "[player_weapon.%s.viewmodel]\n"
        "hands_offset = [%.4f, %.4f, %.4f]\n"
        "hands_rotation = [%.2f, %.2f, %.2f]\n"
        "hands_scale = %.4f\n"
        "fire_duration = %.3f\n"
        "recoil_distance = %.4f\n"
        "recoil_pitch_degrees = %.3f\n"
        "recoil_yaw_degrees = %.3f\n"
        "recoil_recovery = %.3f\n"
        "movement_bob = %.4f\n"
        "movement_bob_speed = %.3f\n"
        "idle_sway = %.4f\n"
        "look_sway = %.5f\n",
        weaponId.c_str(), v.handsOffset.x, v.handsOffset.y, v.handsOffset.z,
        v.handsRotationDegrees.x, v.handsRotationDegrees.y,
        v.handsRotationDegrees.z, v.handsScale, v.fireDuration,
        v.recoilDistance, v.recoilPitchDegrees, v.recoilYawDegrees,
        v.recoilRecovery, v.movementBob, v.movementBobSpeed, v.idleSway,
        v.lookSway);
    return buffer;
}

} // namespace game
