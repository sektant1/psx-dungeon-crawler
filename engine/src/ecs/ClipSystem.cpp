// The clip player: short authored animations over reflected component fields.
//
// See eng/ecs/components/Clip.h for what a clip is and what it deliberately is
// not. This file is the whole runtime: resolve a track's (component, field)
// names once, sample its keys, write the value. Everything that makes it
// general lives in the ComponentRegistry it borrows, not here -- which is why
// adding an animatable component is zero lines in this file.

#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/Systems.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Children.h>
#include <eng/ecs/components/Clip.h>
#include <eng/ecs/components/Dirty.h>
#include <eng/ecs/components/Name.h>
#include <eng/ecs/components/Transform.h>
#include <eng/Log.h>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace eng::ecs {
namespace {

float ease(ClipEase mode, float t)
{
    switch (mode) {
    case ClipEase::Linear:  return t;
    case ClipEase::Smooth:  return t * t * (3.0f - 2.0f * t);
    case ClipEase::EaseIn:  return t * t;
    case ClipEase::EaseOut: return t * (2.0f - t);
    case ClipEase::Step:    return 0.0f;
    }
    return t;
}

// The value of a track at time `t`, in seconds. Clamped at both ends: before
// the first key a track holds the first value and after the last it holds the
// last, so a track that covers only the middle of a clip is a legal way to say
// "this part does not move yet".
glm::vec3 sample(const ClipTrack& track, float t)
{
    const std::vector<ClipKey>& keys = track.keys;
    if (keys.empty())
        return glm::vec3(0.0f);
    if (t <= keys.front().t)
        return keys.front().value;
    if (t >= keys.back().t)
        return keys.back().value;

    // Linear scan. Clips are short by construction -- a handful of keys -- and
    // a binary search here would cost more in code than it saves in cycles.
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const ClipKey& b = keys[i];
        if (t > b.t)
            continue;
        const ClipKey& a = keys[i - 1];
        const float span = b.t - a.t;
        // Two keys at the same time is a step, not a division by zero.
        const float u = span > 1e-6f ? (t - a.t) / span : 1.0f;
        return glm::mix(a.value, b.value, ease(track.ease, u));
    }
    return keys.back().value;
}

// Finds the entity a track drives: the clip's own, or a descendant by Name.
//
// Searched every frame rather than cached as an entity id, and that is
// deliberate: a cached id survives the entity it names being destroyed and
// rebuilt (which a level reload does to everything), and a dangling one writes
// into whatever entt handed the slot to next. The search is over one subtree of
// a handful of children.
entt::entity resolveTarget(const entt::registry& reg, entt::entity self,
                           const std::string& name)
{
    if (name.empty())
        return self;
    const auto* children = reg.try_get<Children>(self);
    if (!children)
        return entt::null;
    for (const entt::entity child : children->value) {
        if (!reg.valid(child))
            continue;
        if (const auto* n = reg.try_get<Name>(child); n && n->value == name)
            return child;
        if (const entt::entity found = resolveTarget(reg, child, name);
            found != entt::null)
            return found;
    }
    return entt::null;
}

} // namespace

entt::entity clipTrackTarget(const entt::registry& reg, entt::entity self,
                             const std::string& target)
{
    if (self == entt::null || !reg.valid(self))
        return entt::null;
    return resolveTarget(reg, self, target);
}

namespace {

// Binds a track's names to indices in the registry's type table. Done once,
// and reported once: a typo in a component or field name is a track that does
// nothing, and silence is the failure mode that costs an afternoon.
void resolveTrack(ClipTrack& track, const ComponentRegistry& types)
{
    track.resolved = true;
    track.typeIndex = -1;
    track.fieldIndex = -1;

    const std::vector<ComponentType>& all = types.types();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (track.component != all[i].name)
            continue;
        for (int f = 0; f < all[i].fieldCount; ++f) {
            if (track.field != all[i].fields[f].name)
                continue;
            const FieldType type = all[i].fields[f].type;
            if (type == FieldType::String || type == FieldType::Quat) {
                log::warn(
                    "Clip: track '%s.%s' has type %s, which a clip cannot "
                    "interpolate. Rotation is authored as euler Vec3 on the "
                    "Transform.",
                    track.component.c_str(), track.field.c_str(),
                    type == FieldType::String ? "String" : "Quat");
                return;
            }
            track.typeIndex = int(i);
            track.fieldIndex = f;
            return;
        }
        log::warn("Clip: component '%s' has no reflected field '%s'.",
                     track.component.c_str(), track.field.c_str());
        return;
    }
    log::warn("Clip: no component type named '%s'.", track.component.c_str());
}

// Sorts a track's keys by time. Used on resolve here and after every drag in
// the Timeline panel, so both agree that a track is stored in time order --
// `sample` scans forward and would read an out-of-order track as a jumble.
void sortKeys(std::vector<ClipKey>& keys)
{
    std::stable_sort(keys.begin(), keys.end(),
                     [](const ClipKey& a, const ClipKey& b) { return a.t < b.t; });
}

// Writes a sampled value onto a live field. `instance` is the component's raw
// bytes, from the registry's own accessor.
//
// Named apply- rather than write- because eng::ecs::writeField already means
// "encode this field into a byte stream" (ComponentRegistry.h). Two functions
// in one namespace called writeField, doing unrelated things, is a five-minute
// misreading waiting to happen.
void applyField(void* instance, const Field& field, const glm::vec3& value)
{
    void* at = fieldPtr(instance, field);
    switch (field.type) {
    case FieldType::Float:
        *static_cast<float*>(at) = value.x;
        break;
    case FieldType::Vec3:
    case FieldType::Colour:
        *static_cast<glm::vec3*>(at) = value;
        break;
    case FieldType::Bool:
        // Any non-zero is true, so a Step track between 0 and 1 is a switch.
        *static_cast<bool*>(at) = value.x != 0.0f;
        break;
    case FieldType::Int:
        *static_cast<int*>(at) = int(std::lround(value.x));
        break;
    case FieldType::String:
    case FieldType::Quat:
        break; // refused at resolve time; unreachable
    }
}

// Advances a clip's playhead and returns the time to sample at.
float advance(Clip& clip, float dt)
{
    const float duration = std::max(clip.duration, 1e-4f);
    clip.time += dt * clip.speed * float(clip.direction);

    switch (clip.mode) {
    case ClipMode::Once:
        if (clip.time >= duration) {
            clip.time = duration;
            clip.playing = false;
            clip.finished = true;
        } else if (clip.time < 0.0f) {
            clip.time = 0.0f;
        }
        break;
    case ClipMode::Loop:
        // fmod rather than a subtract, so a long frame (a level load, a
        // breakpoint) does not leave the playhead several durations past the
        // end and the clip visibly stuck.
        clip.time = std::fmod(clip.time, duration);
        if (clip.time < 0.0f)
            clip.time += duration;
        break;
    case ClipMode::PingPong:
        if (clip.time >= duration) {
            clip.time = duration;
            clip.direction = -1;
        } else if (clip.time <= 0.0f) {
            clip.time = 0.0f;
            clip.direction = 1;
        }
        break;
    }
    return clip.time;
}

} // namespace

void clipSystem(World& world, float dt)
{
    const ComponentRegistry* types = world.componentTypes();
    if (!types)
        return; // no table, no name resolution -- see World::setComponentTypes

    entt::registry& reg = world.registry();
    for (const entt::entity e : reg.view<Clip>()) {
        Clip& clip = reg.get<Clip>(e);

        // autoplay is applied once rather than every frame, so a clip stopped
        // by a script does not restart itself on the next tick.
        if (!clip.started) {
            clip.started = true;
            clip.playing = clip.autoplay;
        }

        const float t = clip.playing ? advance(clip, dt) : clip.time;

        // Nothing to do when the clip is stopped and its pose already matches
        // the playhead. Comparing against the applied time rather than testing
        // `playing` is what makes a *scrub* work -- the Timeline moves `time`
        // on a paused clip and nothing else could tell this system that
        // happened -- while still costing a finished clip nothing per frame.
        if (!clip.playing && t == clip.appliedTime)
            continue;
        clip.appliedTime = t;

        for (ClipTrack& track : clip.tracks) {
            if (!track.resolved) {
                sortKeys(track.keys);
                resolveTrack(track, *types);
            }
            if (track.typeIndex < 0 || track.keys.empty())
                continue;

            const entt::entity target = resolveTarget(reg, e, track.target);
            if (target == entt::null || !reg.valid(target))
                continue;

            const ComponentType& type = types->types()[std::size_t(track.typeIndex)];
            if (!type.instance)
                continue;
            void* instance = type.instance(reg, target);
            if (!instance)
                continue; // the target does not carry that component

            applyField(instance, type.fields[track.fieldIndex], sample(track, t));

            // A written Transform is a moved subtree, and nothing else in the
            // frame knows that happened: the hierarchy resolve is driven by
            // Dirty, not by comparing poses. Tagged here rather than by the
            // caller because a clip is the only thing in the frame that can
            // move an entity without going through World::setLocalTransform.
            //
            // Safe to emplace while iterating reg.view<Clip>(): Dirty is a
            // different pool, and the view being walked is not the one growing.
            if (type.name && std::string_view(type.name) == "Transform")
                reg.emplace_or_replace<Dirty>(target);
        }
    }
}

} // namespace eng::ecs
