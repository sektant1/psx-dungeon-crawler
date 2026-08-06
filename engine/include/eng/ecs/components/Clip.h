#pragma once
#include <glm/glm.hpp>

#include <iterator> // std::size, for the name tables below
#include <string>
#include <string_view>
#include <vector>

namespace eng::ecs {

// A short authored animation: a duration, and a list of tracks that drive
// reflected component fields over it.
//
// The gap this fills. `Spin`, `Orbit` and `LightAnimation` are endless
// procedural modulators -- they have no beginning and no end -- and ozz clips
// are skeletal and need Blender plus gltf2ozz to produce. Everything between
// the two was a Lua script written per instance: a door that opens over 0.8 s,
// a platform that rises, a light that ramps when a lever is pulled, a camera
// that pushes in for a shot. Those are the same animation with different
// numbers, and they were four scripts.
//
// Why a track names its target as *strings*. `component` is a
// ComponentRegistry name and `field` is a reflected field name, resolved once
// through the same table that drives .map serialisation and the inspector. The
// consequence is the point of it: every reflected field in the engine and the
// game is animatable the day it is declared, with no per-component animation
// code, ever. A system nobody wrote for ShaderParams animates ShaderParams.
//
// What this is deliberately NOT. There is no blending between clips, no
// additive layer, no state machine and no root motion -- one clip per entity,
// played or not. A case that needs any of those needs a skeleton, and the
// skeleton already has ozz (see docs/actor-animation.md). Keeping that boundary
// stated is what stops a 300-line clip player being grown into a second
// animation runtime by accretion.

enum class ClipMode : int {
    Once = 0,  // play to the end and stop, holding the last pose
    Loop,      // restart from zero
    PingPong,  // play forward, then backward, forever
};

enum class ClipEase : int {
    Linear = 0,
    Smooth,   // smoothstep: the sane default, no overshoot, no visible corner
    EaseIn,
    EaseOut,
    Step,     // hold the earlier key until the next one -- what a Bool wants
};

// The names of the two enums above, in declaration order, in the two registers
// they are needed in. Here rather than at the four use sites because that is
// what they were: the Timeline and the inspector each had a copy of the display
// strings, and the .scn reader and writer each had half of the on-disk ones --
// so a mode added to the enum could ship as a dropdown that lied, or as a file
// the writer produced and the reader rejected.
//
// The two registers are deliberately separate lists. The display names are free
// to be reworded; the ids are a **file format** and are not.
inline constexpr const char* kClipModeNames[] = {"Once", "Loop", "Ping-Pong"};
inline constexpr const char* kClipEaseNames[] = {"Linear", "Smooth", "Ease In",
                                                 "Ease Out", "Step"};
inline constexpr const char* kClipModeIds[] = {"once", "loop", "pingpong"};
inline constexpr const char* kClipEaseIds[] = {"linear", "smooth", "easein",
                                               "easeout", "step"};

// Derived, never typed: a hand-written count is the thing that goes stale when
// a row is added, and it goes stale silently -- as a dropdown missing its last
// entry, which nobody reads as a bug in a count.
inline constexpr int kClipModeCount = int(std::size(kClipModeNames));
inline constexpr int kClipEaseCount = int(std::size(kClipEaseNames));
static_assert(std::size(kClipModeIds) == std::size(kClipModeNames),
              "every clip mode needs both a display name and an on-disk id");
static_assert(std::size(kClipEaseIds) == std::size(kClipEaseNames),
              "every clip ease needs both a display name and an on-disk id");
// The enums have no Count member (they are serialised values, and a trailing
// Count is one renumbering away from being written to disk), so this is what
// ties the tables to them: adding a row to one without the other fails here.
static_assert(int(ClipEase::Step) + 1 == kClipEaseCount,
              "ClipEase and its name tables have diverged");
static_assert(int(ClipMode::PingPong) + 1 == kClipModeCount,
              "ClipMode and its name tables have diverged");

// id <-> enum, for the .scn reader and writer. Both directions live here so
// they cannot disagree: they were a hand-written if-chain in SceneSource and a
// hand-written switch in SceneWriter, and a mode added to one but not the other
// is a file that saves and will not load.
//
// The lookups are linear over three or five entries, run once per authored
// clip at load time. A map would be more code and slower.
inline const char* clipModeId(ClipMode mode)
{
    const int i = int(mode);
    return (i >= 0 && i < kClipModeCount) ? kClipModeIds[i] : kClipModeIds[0];
}
inline const char* clipEaseId(ClipEase ease)
{
    const int i = int(ease);
    return (i >= 0 && i < kClipEaseCount) ? kClipEaseIds[i] : kClipEaseIds[1];
}
// False when the id is not one this build knows -- the caller reports it with
// the file location it has and this header does not.
inline bool clipModeFromId(std::string_view id, ClipMode& out)
{
    for (int i = 0; i < kClipModeCount; ++i) {
        if (id != kClipModeIds[i])
            continue;
        out = ClipMode(i);
        return true;
    }
    return false;
}
inline bool clipEaseFromId(std::string_view id, ClipEase& out)
{
    for (int i = 0; i < kClipEaseCount; ++i) {
        if (id != kClipEaseIds[i])
            continue;
        out = ClipEase(i);
        return true;
    }
    return false;
}

// The accepted ids, comma-separated, for an error message. Built from the same
// table the parse rejects against, so a message can never list a set the parser
// does not actually accept.
inline std::string clipIdList(const char* const* ids, int count)
{
    std::string out;
    for (int i = 0; i < count; ++i) {
        if (i)
            out += i + 1 == count ? " or " : ", ";
        out += ids[i];
    }
    return out;
}

// One keyframe. `value` carries a Float in .x and a Vec3/Colour whole, so a
// track needs no per-type key storage: the field's own type decides how many
// components are read, and it is known before the key ever is.
struct ClipKey {
    float t = 0.0f;
    glm::vec3 value{0.0f};
};

struct ClipTrack {
    // Which entity this track drives. Empty means the entity carrying the Clip;
    // otherwise a descendant, by Name. Descendants only, deliberately: a clip
    // that reaches across the scene is a dependency the scene file does not
    // record, and it breaks the moment either entity is moved or duplicated.
    // Two entities that must animate together are two clips, or a script.
    std::string target;
    // A ComponentRegistry type name, e.g. "Transform".
    std::string component;
    // A reflected field name on that component, e.g. "position".
    std::string field;
    ClipEase ease = ClipEase::Smooth;
    // Ordered by `t`. clipSystem sorts on resolve rather than trusting the
    // file, because an out-of-order key reads as "the animation is broken"
    // rather than as a bad number, and the fix is two lines here instead of a
    // validator rule and a quick fix.
    std::vector<ClipKey> keys;

    // --- resolved, written by clipSystem, never authored -----------------
    // Set on the first tick and whenever `resolved` is cleared. -1 means the
    // component name matched nothing in the registry; the track is then inert
    // and says so once in the log rather than every frame.
    int typeIndex = -1;
    int fieldIndex = -1;
    bool resolved = false;
};

struct Clip {
    // Seconds. The keys' `t` are in the same units, so retiming a clip is one
    // number and not a rescale of every key.
    float duration = 1.0f;
    ClipMode mode = ClipMode::Once;
    float speed = 1.0f;
    // Whether the clip starts playing when the scene loads. False for anything
    // a trigger, a script or a lever is supposed to start.
    bool autoplay = true;

    // --- runtime ---------------------------------------------------------
    // Deliberately not reflected, for the same reason `Orbit::travelled` and
    // `LightAnimation::time` are not: this is where the clip currently *is*,
    // not how it was authored, and a saved one would reload mid-swing.
    float time = 0.0f;
    bool playing = false;
    bool started = false;  // has autoplay been applied yet
    // Latched true when a Once clip reaches its end, and never cleared by the
    // player. That is what makes it *observable*: a door that has finished
    // opening is a thing gameplay asks about, and a flag the system consumed on
    // the same frame it set could never answer.
    bool finished = false;
    int direction = 1; // PingPong only
    // The playhead the current pose was written from, or a negative value when
    // no pose has been written yet.
    //
    // This is what decides whether a frame has any work to do, and it replaces
    // the obvious `playing` test for two reasons. A stopped clip whose `time`
    // was moved -- by the Timeline scrubbing it, by a script seeking it -- must
    // still re-pose, and nothing tells the system that happened. And a finished
    // clip must NOT re-pose every frame: it writes a Transform, which tags the
    // subtree Dirty, which would keep the hierarchy resolving that branch
    // forever for a clip that has not moved since it stopped.
    float appliedTime = -1.0f;

    std::vector<ClipTrack> tracks;
};

} // namespace eng::ecs
