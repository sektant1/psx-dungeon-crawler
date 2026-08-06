#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eng::animation {

// Immutable cooked skeleton plus named clips. Ozz types remain behind this
// boundary so gameplay, renderer headers, and save data never depend on archive
// layout or SIMD storage.
class AnimationRig final {
public:
    static std::shared_ptr<AnimationRig>
    load(const std::string& skeletonPath, const std::string& clipDirectory,
         std::string* error = nullptr);

    ~AnimationRig();

    AnimationRig(const AnimationRig&) = delete;
    AnimationRig& operator=(const AnimationRig&) = delete;

    bool valid() const;
    int jointCount() const;
    const std::vector<std::string>& jointNames() const;
    int jointIndex(std::string_view name) const;
    std::vector<std::string> clipNames() const;
    bool hasClip(const std::string& name) const;
    float clipDuration(const std::string& name) const;

private:
    AnimationRig();
    struct Impl;
    std::unique_ptr<Impl> mImpl;

    friend class SkeletalAnimator;
    friend class PoseBlender;
    friend class JointMask;
};

struct AnimationPlayOptions {
    float fadeSeconds = 0.10f;
    float speed = 1.0f;
    bool loop = true;
    // Non-looping clips crossfade back automatically. Empty holds final pose.
    std::string returnClip;
    float returnFadeSeconds = 0.10f;
};

// Per-instance ozz playback state. Crossfades snapshot currently blended pose,
// so interrupting draw/fire animations never pops back to an older source clip.
class SkeletalAnimator final {
public:
    SkeletalAnimator();
    ~SkeletalAnimator();
    SkeletalAnimator(SkeletalAnimator&&) noexcept;
    SkeletalAnimator& operator=(SkeletalAnimator&&) noexcept;

    SkeletalAnimator(const SkeletalAnimator&) = delete;
    SkeletalAnimator& operator=(const SkeletalAnimator&) = delete;

    bool setRig(std::shared_ptr<const AnimationRig> rig);
    bool play(const std::string& clip,
              const AnimationPlayOptions& options = {});
    bool update(float dt);

    bool valid() const;
    bool playing() const;
    const std::string& currentClip() const;
    float normalizedTime() const;
    std::span<const glm::mat4> modelMatrices() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

// Per-joint layer weight: which half of the body a layer is allowed to move.
//
// SkeletalAnimator plays one clip and crossfades to the next, which is all a
// viewmodel needs. A body needs more: an actor swings while it walks, flinches
// while it runs, and the legs must not stop to allow it. That is a masked
// layer, and a mask is the smallest thing that expresses it.
//
// Built from a joint subtree rather than a list, because "the upper body" is a
// statement about the skeleton's shape and enumerating twelve joint names in a
// config file is the same statement with twelve chances to be wrong.
class JointMask final {
public:
    JointMask() = default;
    // Every joint under (and including) each named root gets `weight`; the rest
    // get zero. Unknown names are skipped, so a rig without a spine simply
    // yields a mask that moves nothing rather than a hard failure.
    //
    // `feather` grades the weight in over that many joints of depth below each
    // root instead of switching it on at full strength. A hard mask boundary is
    // the documented failure of partial-skeleton blending -- the masked half
    // reads as bolted onto the unmasked half, because one joint is fully driven
    // by the action and its parent is not driven at all. Grading the boundary
    // (Gregory 12.6.4) is what makes a swing look like it comes from the torso.
    static JointMask subtree(const AnimationRig&,
                             std::span<const std::string> roots,
                             float weight = 1.0f, int feather = 0);
    // Everything the other mask does not cover. The pair is what lets one
    // action clip drive the arms at full strength while its legs fade out
    // against a run: two disjoint masks add up without double-counting, which
    // is not true of two overlapping ones.
    static JointMask complementOf(const AnimationRig&, const JointMask&);

    bool valid() const { return !mWeights.empty(); }
    std::span<const float> weights() const { return mWeights; }

private:
    std::vector<float> mWeights;
};

// One layer of a blended pose.
//
// `time` is in seconds and wraps for looping clips, which is what lets the
// caller own playback: locomotion has to hold a foot phase across a walk-to-run
// transition, and it cannot do that if the clip owns the clock.
struct PoseLayer {
    std::string clip;
    float time = 0.0f;
    float weight = 1.0f;
    bool loop = true;
    // Null covers the whole skeleton. Non-owning: masks are built once per rig
    // and outlive any single frame's layer list.
    const JointMask* mask = nullptr;
};

// An extra rotation applied to one joint after blending, before the model
// matrices are built. Procedural on top of authored: a head that turns toward
// what the actor is looking at, a torso that leans into a turn.
//
// `rotation` is in CHARACTER space about the joint's own origin -- "yaw the
// head 20 degrees left" -- not in the joint's local basis, so a clip table and
// an overlay mean the same thing by the same numbers and bone roll never
// enters either.
//
// The conversion into local space uses the joint's REST orientation, which
// makes the overlay exact on a joint whose parents are near their rest pose and
// slightly off on one whose are not: a head look drifts a couple of degrees of
// roll while the chest is rotated mid-stride. The exact version needs the
// global pose and therefore a second local-to-model pass after post-processing
// (Gregory's stages 4-5); this one runs between stages 2 and 3 for free, and
// for head aim and lean the error is not visible. Anything that must be exact
// -- IK, foot planting -- wants the two-pass form and does not belong here.
struct JointOverlay {
    int joint = -1;
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

// Samples and blends N clips into one pose.
//
// Deliberately stateless between frames: it holds scratch buffers and the rig,
// not a playhead. Whoever owns the animation state machine owns the clocks, and
// the blender only answers "what does this combination look like".
class PoseBlender final {
public:
    PoseBlender();
    ~PoseBlender();
    PoseBlender(PoseBlender&&) noexcept;
    PoseBlender& operator=(PoseBlender&&) noexcept;

    PoseBlender(const PoseBlender&) = delete;
    PoseBlender& operator=(const PoseBlender&) = delete;

    bool setRig(std::shared_ptr<const AnimationRig> rig);
    const AnimationRig* rig() const;

    // Layers blend in order; weights are normalised by ozz against the rest
    // pose, so a set that does not sum to one leans toward the rest pose rather
    // than exploding. Layers naming a missing clip are skipped -- a rig missing
    // `run_f` should walk, not crash.
    bool evaluate(std::span<const PoseLayer> layers,
                  std::span<const JointOverlay> overlays = {});

    bool valid() const;
    std::span<const glm::mat4> modelMatrices() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng::animation
