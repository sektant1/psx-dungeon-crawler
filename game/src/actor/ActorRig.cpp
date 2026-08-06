#include "ActorRig.h"

#include "GameAssets.h"

#include <eng/Log.h>
#include <eng/Renderer.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>

namespace game::actor {
namespace {

float number(const toml::table& table, const char* key, float fallback)
{
    return float(table[key].value_or(double(fallback)));
}

std::vector<std::string> stringList(const toml::node_view<const toml::node>& node,
                                    std::vector<std::string> fallback)
{
    const toml::array* values = node.as_array();
    if (!values)
        return fallback;
    std::vector<std::string> parsed;
    for (const toml::node& entry : *values)
        if (auto text = entry.value<std::string>())
            parsed.push_back(*text);
    return parsed.empty() ? fallback : parsed;
}

std::vector<float> floatList(const toml::node_view<const toml::node>& node,
                             std::vector<float> fallback)
{
    const toml::array* values = node.as_array();
    if (!values)
        return fallback;
    std::vector<float> parsed;
    for (const toml::node& entry : *values)
        parsed.push_back(float(entry.value_or(0.0)));
    return parsed.empty() ? fallback : parsed;
}

void parseClips(const toml::table& table, ActorClipNames& clips)
{
    // A row present means "play this instead"; a row absent means the built-in
    // name, so a rig only has to name what it renamed.
    const std::pair<const char*, std::string*> rows[] = {
        {"idle", &clips.idle},
        {"idle_combat", &clips.idleCombat},
        {"dormant", &clips.dormant},
        {"talk", &clips.talk},
        {"walk_forward", &clips.walkForward},
        {"walk_back", &clips.walkBack},
        {"walk_left", &clips.walkLeft},
        {"walk_right", &clips.walkRight},
        {"run_forward", &clips.runForward},
        {"run_back", &clips.runBack},
        {"jump", &clips.jump},
        {"fall", &clips.fall},
        {"land", &clips.land},
        {"attack_light", &clips.attackLight},
        {"attack_alternate", &clips.attackAlternate},
        {"attack_heavy", &clips.attackHeavy},
        {"cast", &clips.cast},
        {"hit", &clips.hit},
        {"stagger", &clips.stagger},
        {"death", &clips.death},
    };
    for (const auto& [key, target] : rows)
        *target = table[key].value_or(*target);
}

bool parseTable(const toml::table& root, ActorRigDef& out)
{
    const toml::table* actor = root["actor"].as_table();
    if (!actor)
        return true; // no section is not an error: the default is the ship

    ActorRigDef parsed = out;
    parsed.skeleton = (*actor)["skeleton"].value_or(parsed.skeleton);
    parsed.clipDirectory =
        (*actor)["clip_directory"].value_or(parsed.clipDirectory);
    parsed.model = (*actor)["model"].value_or(parsed.model);
    parsed.material = (*actor)["material"].value_or(parsed.material);
    parsed.height = number(*actor, "height", parsed.height);
    parsed.upperBody = stringList((*actor)["upper_body"], parsed.upperBody);

    if (const toml::table* clips = (*actor)["clips"].as_table())
        parseClips(*clips, parsed.clips);

    if (const toml::table* locomotion = (*actor)["locomotion"].as_table()) {
        ActorLocomotionTuning& tuning = parsed.locomotion;
        tuning.walkStride = number(*locomotion, "walk_stride", tuning.walkStride);
        tuning.runStride = number(*locomotion, "run_stride", tuning.runStride);
        tuning.idleSpeed = number(*locomotion, "idle_speed", tuning.idleSpeed);
        tuning.walkSpeed = number(*locomotion, "walk_speed", tuning.walkSpeed);
        tuning.runSpeed = number(*locomotion, "run_speed", tuning.runSpeed);
        tuning.minCadence = number(*locomotion, "min_cadence", tuning.minCadence);
        tuning.maxCadence = number(*locomotion, "max_cadence", tuning.maxCadence);
        tuning.postureBlend =
            number(*locomotion, "posture_blend", tuning.postureBlend);
    }

    if (const toml::table* action = (*actor)["action"].as_table()) {
        ActorActionTuning& tuning = parsed.action;
        tuning.blendIn = number(*action, "blend_in", tuning.blendIn);
        tuning.blendOut = number(*action, "blend_out", tuning.blendOut);
        tuning.hitBlendIn = number(*action, "hit_blend_in", tuning.hitBlendIn);
        tuning.hitBlendOut = number(*action, "hit_blend_out", tuning.hitBlendOut);
    }

    if (const toml::table* look = (*actor)["look"].as_table()) {
        ActorLookTuning& tuning = parsed.look;
        tuning.joints = stringList((*look)["joints"], tuning.joints);
        tuning.share = floatList((*look)["share"], tuning.share);
        tuning.maxYawDegrees =
            number(*look, "max_yaw_degrees", tuning.maxYawDegrees);
        tuning.maxPitchDegrees =
            number(*look, "max_pitch_degrees", tuning.maxPitchDegrees);
        tuning.responsiveness =
            number(*look, "responsiveness", tuning.responsiveness);
    }

    // A stride of zero divides by itself when converting speed to cadence, and
    // an idle threshold above the walk speed means an actor that never walks.
    // Both are the kind of typo that reads as "animation is broken".
    if (!(parsed.height > 0.0f) || !(parsed.locomotion.walkStride > 0.0f) ||
        !(parsed.locomotion.runStride > 0.0f) ||
        !(parsed.locomotion.walkSpeed > parsed.locomotion.idleSpeed) ||
        !(parsed.locomotion.runSpeed > parsed.locomotion.walkSpeed) ||
        !(parsed.locomotion.maxCadence > parsed.locomotion.minCadence)) {
        eng::log::error("actors.toml: locomotion thresholds are not ordered");
        return false;
    }

    out = std::move(parsed);
    return true;
}

} // namespace

bool parseActorRigDef(const char* tomlSource, ActorRigDef& out)
{
    const toml::parse_result result = toml::parse(tomlSource);
    if (!result) {
        eng::log::error("actors.toml: %s",
                        std::string(result.error().description()).c_str());
        return false;
    }
    return parseTable(result.table(), out);
}

bool loadActorRigDef(const std::string& tomlPath, ActorRigDef& out)
{
    const toml::parse_result result = toml::parse_file(tomlPath);
    if (!result) {
        eng::log::warn("actors: cannot read '%s' (%s); using built-in rig",
                       tomlPath.c_str(),
                       std::string(result.error().description()).c_str());
        return false;
    }
    return parseTable(result.table(), out);
}

bool ActorRig::load(eng::Renderer& renderer, const ActorRigDef& def)
{
    unload(renderer);
    mDef = def;

    std::string error;
    mRig = eng::animation::AnimationRig::load(assetPath(def.skeleton),
                                              assetPath(def.clipDirectory),
                                              &error);
    if (!mRig) {
        eng::log::error("actors: rig '%s' failed: %s", def.skeleton.c_str(),
                        error.c_str());
        return false;
    }

    mMesh = renderer.loadSkinnedMesh(assetPath(def.model), mRig->jointNames());
    if (!mMesh.valid()) {
        eng::log::error("actors: skinned mesh '%s' failed", def.model.c_str());
        mRig.reset();
        return false;
    }

    // Graded over two joints of spine rather than switched on at one: a hard
    // mask boundary is what makes an upper-body swing look bolted to a lower
    // body that never heard about it.
    mUpperBody =
        eng::animation::JointMask::subtree(*mRig, def.upperBody, 1.0f, 2);
    mLowerBody = eng::animation::JointMask::complementOf(*mRig, mUpperBody);

    // The look chain, resolved to indices with its shares renormalised: an
    // author who writes three joints and two shares gets an even split of what
    // is left rather than a head that turns 60% of the way and stops.
    mLookChain.clear();
    float total = 0.0f;
    for (size_t index = 0; index < def.look.joints.size(); ++index) {
        const int joint = mRig->jointIndex(def.look.joints[index]);
        if (joint < 0) {
            eng::log::warn("actors: look joint '%s' is not on this skeleton",
                           def.look.joints[index].c_str());
            continue;
        }
        const float share =
            index < def.look.share.size() ? def.look.share[index] : 0.0f;
        mLookChain.emplace_back(joint, std::max(0.0f, share));
        total += std::max(0.0f, share);
    }
    if (total > 1e-4f) {
        for (auto& [joint, share] : mLookChain)
            share /= total;
    } else if (!mLookChain.empty()) {
        const float even = 1.0f / float(mLookChain.size());
        for (auto& [joint, share] : mLookChain)
            share = even;
    }

    eng::log::info("actors: humanoid rig loaded (%d joints, %zu clips)",
                   mRig->jointCount(), mRig->clipNames().size());
    return true;
}

void ActorRig::unload(eng::Renderer& renderer)
{
    if (mMesh.valid())
        renderer.releaseSkinnedMesh(mMesh);
    mMesh = {};
    mRig.reset();
    mUpperBody = {};
    mLowerBody = {};
    mLookChain.clear();
}

float ActorRig::clipDuration(const std::string& clip) const
{
    return mRig ? mRig->clipDuration(clip) : 0.0f;
}

bool ActorRig::hasClip(const std::string& clip) const
{
    return mRig && mRig->hasClip(clip);
}

const std::string& ActorRig::clipFor(ActorAction action) const
{
    switch (action) {
    case ActorAction::AttackLight: return mDef.clips.attackLight;
    case ActorAction::AttackAlternate: return mDef.clips.attackAlternate;
    case ActorAction::AttackHeavy: return mDef.clips.attackHeavy;
    case ActorAction::Cast: return mDef.clips.cast;
    case ActorAction::Hit: return mDef.clips.hit;
    case ActorAction::Stagger: return mDef.clips.stagger;
    case ActorAction::Death: return mDef.clips.death;
    case ActorAction::None: break;
    }
    return mDef.clips.idle;
}

float ActorRig::clipSpeedFor(ActorAction action, float seconds) const
{
    const float authored = clipDurationFor(action);
    if (!(authored > 0.0f) || !std::isfinite(seconds) || !(seconds > 0.01f))
        return 1.0f;
    return std::clamp(authored / seconds, 0.5f, 2.0f);
}

} // namespace game::actor
