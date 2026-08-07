#include <eng/animation/SkeletalAnimation.h>

#include <eng/Log.h>

// ozz's SimdFloat4 is __m128, whose 16-byte alignment is an attribute rather
// than part of the type, so every template that takes one -- ozz's own spans as
// well as the containers below -- makes the compiler announce that it is
// dropping that attribute. It is not being dropped in any way that matters:
// ozz::vector's allocator is what honours the alignment. Silenced across the
// whole ozz surface because the diagnostic fires at each instantiation, inside
// headers this file does not own.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/containers/vector.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <utility>

namespace eng::animation {
namespace {

constexpr uintmax_t kMaxArchiveBytes = 64u * 1024u * 1024u;

template <typename T>
bool loadArchive(const std::filesystem::path& path, T& out, std::string& error)
{
    std::error_code filesystemError;
    const uintmax_t size = std::filesystem::file_size(path, filesystemError);
    if (filesystemError || size == 0 || size > kMaxArchiveBytes) {
        error = "invalid archive size for '" + path.string() + "'";
        return false;
    }

    ozz::io::File stream(path.string().c_str(), "rb");
    if (!stream.opened()) {
        error = "cannot open archive '" + path.string() + "'";
        return false;
    }
    ozz::io::IArchive archive(&stream);
    if (!archive.TestTag<T>()) {
        error = "archive type does not match '" + path.string() + "'";
        return false;
    }
    archive >> out;
    return true;
}

bool finiteMatrix(const glm::mat4& matrix)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                return false;
    return true;
}

} // namespace

struct AnimationRig::Impl {
    struct Clip {
        ozz::animation::Animation animation;
    };

    ozz::animation::Skeleton skeleton;
    std::vector<std::string> jointNames;
    std::unordered_map<std::string, int> jointIndices;
    std::unordered_map<std::string, std::unique_ptr<Clip>> clips;
};

AnimationRig::AnimationRig() : mImpl(std::make_unique<Impl>()) {}
AnimationRig::~AnimationRig() = default;

std::shared_ptr<AnimationRig>
AnimationRig::load(const std::string& skeletonPath,
                   const std::string& clipDirectory, std::string* outputError)
{
    auto fail = [&](std::string message) -> std::shared_ptr<AnimationRig> {
        if (outputError)
            *outputError = message;
        log::error("Animation rig: %s", message.c_str());
        return {};
    };

    auto rig = std::shared_ptr<AnimationRig>(new AnimationRig());
    std::string error;
    if (!loadArchive(skeletonPath, rig->mImpl->skeleton, error))
        return fail(std::move(error));
    if (rig->mImpl->skeleton.num_joints() <= 0)
        return fail("skeleton contains no joints");

    rig->mImpl->jointNames.reserve(
        size_t(rig->mImpl->skeleton.num_joints()));
    for (const char* name : rig->mImpl->skeleton.joint_names()) {
        rig->mImpl->jointNames.emplace_back(name ? name : "");
        const int index = int(rig->mImpl->jointNames.size() - 1);
        if (!rig->mImpl->jointNames.back().empty() &&
            !rig->mImpl->jointIndices
                 .emplace(rig->mImpl->jointNames.back(), index)
                 .second)
            return fail("skeleton contains duplicate joint '" +
                        rig->mImpl->jointNames.back() + "'");
    }

    std::vector<std::filesystem::path> archives;
    std::error_code directoryError;
    for (std::filesystem::directory_iterator it(clipDirectory, directoryError),
         end;
         !directoryError && it != end; it.increment(directoryError)) {
        if (it->is_regular_file() && it->path().extension() == ".ozz" &&
            it->path() != std::filesystem::path(skeletonPath))
            archives.push_back(it->path());
    }
    if (directoryError)
        return fail("cannot enumerate clip directory '" + clipDirectory + "'");
    std::sort(archives.begin(), archives.end());

    for (const std::filesystem::path& path : archives) {
        auto clip = std::make_unique<Impl::Clip>();
        if (!loadArchive(path, clip->animation, error))
            continue; // Directory may also contain other tagged ozz resources.
        if (clip->animation.num_tracks() != rig->mImpl->skeleton.num_joints())
            return fail("clip '" + path.string() +
                        "' does not match skeleton joint count");
        if (!(clip->animation.duration() > 0.0f) ||
            !std::isfinite(clip->animation.duration()))
            return fail("clip '" + path.string() + "' has invalid duration");
        std::string name = clip->animation.name();
        if (name.empty())
            name = path.stem().string();
        if (!rig->mImpl->clips.emplace(name, std::move(clip)).second)
            return fail("duplicate animation clip '" + name + "'");
    }
    if (rig->mImpl->clips.empty())
        return fail("clip directory contains no matching animation archives");
    if (outputError)
        outputError->clear();
    return rig;
}

bool AnimationRig::valid() const
{
    return mImpl && mImpl->skeleton.num_joints() > 0 && !mImpl->clips.empty();
}

int AnimationRig::jointCount() const
{
    return mImpl ? mImpl->skeleton.num_joints() : 0;
}

const std::vector<std::string>& AnimationRig::jointNames() const
{
    static const std::vector<std::string> empty;
    return mImpl ? mImpl->jointNames : empty;
}

int AnimationRig::jointIndex(std::string_view name) const
{
    if (!mImpl)
        return -1;
    const auto found = mImpl->jointIndices.find(std::string(name));
    return found == mImpl->jointIndices.end() ? -1 : found->second;
}

std::vector<std::string> AnimationRig::clipNames() const
{
    std::vector<std::string> result;
    if (!mImpl)
        return result;
    result.reserve(mImpl->clips.size());
    for (const auto& [name, clip] : mImpl->clips)
        result.push_back(name);
    std::sort(result.begin(), result.end());
    return result;
}

bool AnimationRig::hasClip(const std::string& name) const
{
    return mImpl && mImpl->clips.contains(name);
}

float AnimationRig::clipDuration(const std::string& name) const
{
    if (!mImpl)
        return 0.0f;
    const auto found = mImpl->clips.find(name);
    return found == mImpl->clips.end() ? 0.0f
                                       : found->second->animation.duration();
}

struct SkeletalAnimator::Impl {
    std::shared_ptr<const AnimationRig> rig;
    const AnimationRig::Impl::Clip* clip = nullptr;
    ozz::animation::SamplingJob::Context samplingContext;
    ozz::vector<ozz::math::SoaTransform> sampledLocals;
    ozz::vector<ozz::math::SoaTransform> sourceLocals;
    ozz::vector<ozz::math::SoaTransform> poseLocals;
    ozz::vector<ozz::math::Float4x4> modelTransforms;
    std::vector<glm::mat4> modelMatrices;
    std::string clipName;
    AnimationPlayOptions options;
    float time = 0.0f;
    float fadeElapsed = 0.0f;
    bool poseReady = false;

    bool rebuildModels()
    {
        if (!rig || !rig->valid())
            return false;
        ozz::animation::LocalToModelJob job;
        job.skeleton = &rig->mImpl->skeleton;
        job.input = ozz::make_span(poseLocals);
        job.output = ozz::make_span(modelTransforms);
        if (!job.Run())
            return false;

        for (size_t i = 0; i < modelTransforms.size(); ++i) {
            glm::mat4 matrix(1.0f);
            float* values = glm::value_ptr(matrix);
            ozz::math::StorePtrU(modelTransforms[i].cols[0], values + 0);
            ozz::math::StorePtrU(modelTransforms[i].cols[1], values + 4);
            ozz::math::StorePtrU(modelTransforms[i].cols[2], values + 8);
            ozz::math::StorePtrU(modelTransforms[i].cols[3], values + 12);
            modelMatrices[i] = finiteMatrix(matrix) ? matrix : glm::mat4(1.0f);
        }
        poseReady = true;
        return true;
    }

    bool begin(const std::string& name, AnimationPlayOptions requested)
    {
        if (!rig || !rig->valid())
            return false;
        const auto found = rig->mImpl->clips.find(name);
        if (found == rig->mImpl->clips.end())
            return false;
        requested.fadeSeconds = std::isfinite(requested.fadeSeconds)
                                    ? std::max(0.0f, requested.fadeSeconds)
                                    : 0.0f;
        requested.returnFadeSeconds =
            std::isfinite(requested.returnFadeSeconds)
                ? std::max(0.0f, requested.returnFadeSeconds)
                : 0.0f;
        requested.speed = std::isfinite(requested.speed)
                              ? std::max(0.001f, requested.speed)
                              : 1.0f;

        if (clip == found->second.get() && options.loop && requested.loop) {
            options.returnClip = std::move(requested.returnClip);
            options.returnFadeSeconds = requested.returnFadeSeconds;
            options.speed = requested.speed;
            return true;
        }

        sourceLocals = poseLocals;
        clip = found->second.get();
        clipName = name;
        options = std::move(requested);
        time = 0.0f;
        fadeElapsed = options.fadeSeconds > 0.0f ? 0.0f : options.fadeSeconds;
        samplingContext.Invalidate();
        return true;
    }
};

SkeletalAnimator::SkeletalAnimator() : mImpl(std::make_unique<Impl>()) {}
SkeletalAnimator::~SkeletalAnimator() = default;
SkeletalAnimator::SkeletalAnimator(SkeletalAnimator&&) noexcept = default;
SkeletalAnimator& SkeletalAnimator::operator=(SkeletalAnimator&&) noexcept =
    default;

bool SkeletalAnimator::setRig(std::shared_ptr<const AnimationRig> rig)
{
    if (!rig || !rig->valid())
        return false;
    mImpl->rig = std::move(rig);
    const int jointCount = mImpl->rig->mImpl->skeleton.num_joints();
    const int soaCount = mImpl->rig->mImpl->skeleton.num_soa_joints();
    mImpl->samplingContext.Resize(jointCount);
    mImpl->sampledLocals.resize(size_t(soaCount));
    mImpl->sourceLocals.assign(
        mImpl->rig->mImpl->skeleton.joint_rest_poses().begin(),
        mImpl->rig->mImpl->skeleton.joint_rest_poses().end());
    mImpl->poseLocals = mImpl->sourceLocals;
    mImpl->modelTransforms.resize(size_t(jointCount));
    mImpl->modelMatrices.resize(size_t(jointCount), glm::mat4(1.0f));
    mImpl->clip = nullptr;
    mImpl->clipName.clear();
    mImpl->time = 0.0f;
    mImpl->fadeElapsed = 0.0f;
    mImpl->poseReady = false;
    return mImpl->rebuildModels();
}

bool SkeletalAnimator::play(const std::string& clip,
                            const AnimationPlayOptions& options)
{
    return mImpl->begin(clip, options);
}

bool SkeletalAnimator::update(float dt)
{
    if (!mImpl->rig || !mImpl->clip)
        return false;
    dt = std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
    const float duration = mImpl->clip->animation.duration();
    mImpl->time += dt * mImpl->options.speed;
    bool finished = false;
    float sampleTime = mImpl->time;
    if (mImpl->options.loop) {
        sampleTime = std::fmod(sampleTime, duration);
        if (sampleTime < 0.0f)
            sampleTime += duration;
    } else if (sampleTime >= duration) {
        sampleTime = duration;
        finished = true;
    }

    ozz::animation::SamplingJob sampling;
    sampling.animation = &mImpl->clip->animation;
    sampling.context = &mImpl->samplingContext;
    sampling.ratio = std::clamp(sampleTime / duration, 0.0f, 1.0f);
    sampling.output = ozz::make_span(mImpl->sampledLocals);
    if (!sampling.Run())
        return false;

    mImpl->fadeElapsed = std::min(mImpl->options.fadeSeconds,
                                  mImpl->fadeElapsed + dt);
    const float blend = mImpl->options.fadeSeconds > 0.0f
                            ? std::clamp(mImpl->fadeElapsed /
                                             mImpl->options.fadeSeconds,
                                         0.0f, 1.0f)
                            : 1.0f;
    if (blend < 1.0f) {
        ozz::animation::BlendingJob::Layer layers[2];
        layers[0].transform = ozz::make_span(mImpl->sourceLocals);
        layers[0].weight = 1.0f - blend;
        layers[1].transform = ozz::make_span(mImpl->sampledLocals);
        layers[1].weight = blend;
        ozz::animation::BlendingJob blending;
        blending.layers = layers;
        blending.rest_pose = mImpl->rig->mImpl->skeleton.joint_rest_poses();
        blending.output = ozz::make_span(mImpl->poseLocals);
        if (!blending.Run())
            return false;
    } else {
        mImpl->poseLocals = mImpl->sampledLocals;
    }

    if (!mImpl->rebuildModels())
        return false;

    if (finished && !mImpl->options.returnClip.empty()) {
        AnimationPlayOptions next;
        next.fadeSeconds = mImpl->options.returnFadeSeconds;
        next.loop = true;
        const std::string returnClip = mImpl->options.returnClip;
        if (!mImpl->begin(returnClip, next))
            log::warn("Animation rig: return clip '%s' is missing",
                      returnClip.c_str());
    }
    return true;
}

bool SkeletalAnimator::valid() const
{
    return mImpl && mImpl->rig && mImpl->rig->valid();
}

bool SkeletalAnimator::playing() const
{
    return valid() && mImpl->clip;
}

const std::string& SkeletalAnimator::currentClip() const
{
    static const std::string empty;
    return mImpl ? mImpl->clipName : empty;
}

float SkeletalAnimator::normalizedTime() const
{
    if (!playing())
        return 0.0f;
    const float duration = mImpl->clip->animation.duration();
    if (mImpl->options.loop)
        return std::fmod(mImpl->time, duration) / duration;
    return std::clamp(mImpl->time / duration, 0.0f, 1.0f);
}

std::span<const glm::mat4> SkeletalAnimator::modelMatrices() const
{
    if (!mImpl || !mImpl->poseReady)
        return {};
    return mImpl->modelMatrices;
}

} // namespace eng::animation

// --- masked, layered blending -----------------------------------------------

namespace eng::animation {
namespace {

// Reads one joint's rotation out of a SoA lane and writes a new one back.
// ozz stores four joints per register, so touching a single joint means
// unpacking the register it lives in -- which is only worth doing for the
// handful of procedural overlays, never in a loop over the skeleton.
glm::quat readRotation(const ozz::math::SoaQuaternion& soa, int lane)
{
    float x[4], y[4], z[4], w[4];
    ozz::math::StorePtrU(soa.x, x);
    ozz::math::StorePtrU(soa.y, y);
    ozz::math::StorePtrU(soa.z, z);
    ozz::math::StorePtrU(soa.w, w);
    return glm::quat(w[lane], x[lane], y[lane], z[lane]);
}

void writeRotation(ozz::math::SoaQuaternion& soa, int lane, const glm::quat& q)
{
    float x[4], y[4], z[4], w[4];
    ozz::math::StorePtrU(soa.x, x);
    ozz::math::StorePtrU(soa.y, y);
    ozz::math::StorePtrU(soa.z, z);
    ozz::math::StorePtrU(soa.w, w);
    x[lane] = q.x;
    y[lane] = q.y;
    z[lane] = q.z;
    w[lane] = q.w;
    soa.x = ozz::math::simd_float4::LoadPtrU(x);
    soa.y = ozz::math::simd_float4::LoadPtrU(y);
    soa.z = ozz::math::simd_float4::LoadPtrU(z);
    soa.w = ozz::math::simd_float4::LoadPtrU(w);
}

} // namespace

JointMask JointMask::subtree(const AnimationRig& rig,
                             std::span<const std::string> roots, float weight,
                             int feather)
{
    JointMask mask;
    if (!rig.valid())
        return mask;
    const ozz::animation::Skeleton& skeleton = rig.mImpl->skeleton;
    const int count = skeleton.num_joints();
    mask.mWeights.assign(size_t(count), 0.0f);
    weight = std::isfinite(weight) ? std::clamp(weight, 0.0f, 1.0f) : 1.0f;
    feather = std::max(0, feather);

    // Depth below the nearest mask root, or -1 for joints outside the mask.
    std::vector<int> depth(size_t(count), -1);
    for (const std::string& root : roots) {
        const int index = rig.jointIndex(root);
        if (index >= 0)
            depth[size_t(index)] = 0;
    }
    // Joints are stored parent-before-child, so one forward pass carries the
    // depth down every subtree.
    const ozz::span<const int16_t> parents = skeleton.joint_parents();
    for (int joint = 0; joint < count; ++joint) {
        if (depth[size_t(joint)] >= 0)
            continue;
        const int parent = parents[joint];
        if (parent >= 0 && depth[size_t(parent)] >= 0)
            depth[size_t(joint)] = depth[size_t(parent)] + 1;
    }

    for (int joint = 0; joint < count; ++joint) {
        if (depth[size_t(joint)] < 0)
            continue;
        // Ramp from 1/(feather+1) at the root to full strength `feather`
        // joints down, so the action's authority grows along the chain rather
        // than switching on at one vertebra.
        const float ramp =
            feather == 0
                ? 1.0f
                : std::min(1.0f, float(depth[size_t(joint)] + 1) /
                                     float(feather + 1));
        mask.mWeights[size_t(joint)] = weight * ramp;
    }
    return mask;
}

JointMask JointMask::complementOf(const AnimationRig& rig, const JointMask& other)
{
    JointMask mask;
    if (!rig.valid())
        return mask;
    const size_t count = size_t(rig.jointCount());
    mask.mWeights.assign(count, 1.0f);
    const std::span<const float> source = other.weights();
    for (size_t joint = 0; joint < count && joint < source.size(); ++joint)
        mask.mWeights[joint] = 1.0f - std::clamp(source[joint], 0.0f, 1.0f);
    return mask;
}

struct PoseBlender::Impl {
    std::shared_ptr<const AnimationRig> rig;
    // Rest orientation per joint, in model space. Overlays are authored in
    // character space and this is what converts them; see JointOverlay.
    std::vector<glm::quat> restRotations;
    // One sampling context per layer slot. Contexts cache where in the clip the
    // last sample landed; sharing one across layers throws that away every
    // frame and turns a blend into a search.
    std::vector<std::unique_ptr<ozz::animation::SamplingJob::Context>> contexts;
    std::vector<ozz::vector<ozz::math::SoaTransform>> sampled;
    // SimdFloat4 is __m128, whose 16-byte alignment is an attribute rather than
    // part of the type, so naming it as a template argument makes the compiler
    // say it is dropping that attribute. It is not: ozz::vector's allocator is
    // the one that honours the alignment, which is why this member uses it and
    // why the diagnostic is noise here specifically.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif
    std::vector<ozz::vector<ozz::math::SimdFloat4>> maskBuffers;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    ozz::vector<ozz::math::SoaTransform> poseLocals;
    ozz::vector<ozz::math::Float4x4> modelTransforms;
    std::vector<glm::mat4> modelMatrices;
    bool poseReady = false;

    void reserve(size_t layers)
    {
        const int soaCount = rig->mImpl->skeleton.num_soa_joints();
        const int jointCount = rig->mImpl->skeleton.num_joints();
        while (contexts.size() < layers) {
            auto context = std::make_unique<ozz::animation::SamplingJob::Context>();
            context->Resize(jointCount);
            contexts.push_back(std::move(context));
            sampled.emplace_back(size_t(soaCount));
            maskBuffers.emplace_back();
        }
    }
};

PoseBlender::PoseBlender() : mImpl(std::make_unique<Impl>()) {}
PoseBlender::~PoseBlender() = default;
PoseBlender::PoseBlender(PoseBlender&&) noexcept = default;
PoseBlender& PoseBlender::operator=(PoseBlender&&) noexcept = default;

bool PoseBlender::setRig(std::shared_ptr<const AnimationRig> rig)
{
    if (!rig || !rig->valid())
        return false;
    mImpl->rig = std::move(rig);
    const ozz::animation::Skeleton& skeleton = mImpl->rig->mImpl->skeleton;
    mImpl->contexts.clear();
    mImpl->sampled.clear();
    mImpl->maskBuffers.clear();
    mImpl->poseLocals.assign(skeleton.joint_rest_poses().begin(),
                             skeleton.joint_rest_poses().end());
    mImpl->modelTransforms.resize(size_t(skeleton.num_joints()));
    mImpl->modelMatrices.assign(size_t(skeleton.num_joints()), glm::mat4(1.0f));
    mImpl->poseReady = false;
    if (!evaluate({}))
        return false;

    // The rest pose the blender just produced, kept as orientations: overlays
    // are converted through it every frame and recomputing it per overlay would
    // be a matrix decomposition in the inner loop.
    mImpl->restRotations.clear();
    mImpl->restRotations.reserve(mImpl->modelMatrices.size());
    for (const glm::mat4& matrix : mImpl->modelMatrices)
        mImpl->restRotations.push_back(glm::normalize(glm::quat_cast(matrix)));
    return true;
}

const AnimationRig* PoseBlender::rig() const { return mImpl->rig.get(); }

bool PoseBlender::valid() const
{
    return mImpl && mImpl->rig && mImpl->rig->valid();
}

bool PoseBlender::evaluate(std::span<const PoseLayer> layers,
                           std::span<const JointOverlay> overlays)
{
    if (!valid())
        return false;
    const AnimationRig::Impl& rig = *mImpl->rig->mImpl;
    const int soaCount = rig.skeleton.num_soa_joints();
    mImpl->reserve(layers.size());

    std::vector<ozz::animation::BlendingJob::Layer> jobLayers;
    jobLayers.reserve(layers.size());
    size_t slot = 0;
    for (const PoseLayer& layer : layers) {
        if (!(layer.weight > 0.0f) || !std::isfinite(layer.weight))
            continue;
        const auto found = rig.clips.find(layer.clip);
        if (found == rig.clips.end())
            continue;
        const ozz::animation::Animation& animation = found->second->animation;
        const float duration = animation.duration();
        float time = std::isfinite(layer.time) ? layer.time : 0.0f;
        if (layer.loop) {
            time = std::fmod(time, duration);
            if (time < 0.0f)
                time += duration;
        }

        ozz::animation::SamplingJob sampling;
        sampling.animation = &animation;
        sampling.context = mImpl->contexts[slot].get();
        sampling.ratio = std::clamp(time / duration, 0.0f, 1.0f);
        sampling.output = ozz::make_span(mImpl->sampled[slot]);
        if (!sampling.Run())
            return false;

        ozz::animation::BlendingJob::Layer job;
        job.transform = ozz::make_span(mImpl->sampled[slot]);
        job.weight = layer.weight;
        if (layer.mask && layer.mask->valid()) {
            // Per-joint weights are SoA too: four joints to a register, with
            // the tail lanes of the last register padded to zero.
            auto& buffer = mImpl->maskBuffers[slot];
            buffer.resize(size_t(soaCount));
            const std::span<const float> weights = layer.mask->weights();
            for (int soa = 0; soa < soaCount; ++soa) {
                float lanes[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                for (int lane = 0; lane < 4; ++lane) {
                    const size_t joint = size_t(soa * 4 + lane);
                    lanes[lane] = joint < weights.size() ? weights[joint] : 0.0f;
                }
                buffer[size_t(soa)] = ozz::math::simd_float4::LoadPtrU(lanes);
            }
            job.joint_weights = ozz::make_span(buffer);
        }
        jobLayers.push_back(job);
        ++slot;
    }

    if (jobLayers.empty()) {
        mImpl->poseLocals.assign(rig.skeleton.joint_rest_poses().begin(),
                                 rig.skeleton.joint_rest_poses().end());
    } else {
        ozz::animation::BlendingJob blending;
        blending.layers = ozz::make_span(jobLayers);
        blending.rest_pose = rig.skeleton.joint_rest_poses();
        blending.output = ozz::make_span(mImpl->poseLocals);
        if (!blending.Run())
            return false;
    }

    for (const JointOverlay& overlay : overlays) {
        if (overlay.joint < 0 || overlay.joint >= rig.skeleton.num_joints())
            continue;
        const int soa = overlay.joint / 4;
        const int lane = overlay.joint % 4;
        ozz::math::SoaQuaternion& rotation = mImpl->poseLocals[size_t(soa)].rotation;
        const glm::quat blended = readRotation(rotation, lane);
        // Character space -> this joint's basis: R = M^-1 . D . M.
        const glm::quat rest = size_t(overlay.joint) < mImpl->restRotations.size()
                                   ? mImpl->restRotations[size_t(overlay.joint)]
                                   : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::quat local = glm::inverse(rest) * overlay.rotation * rest;
        const glm::quat result = glm::normalize(blended * local);
        if (std::isfinite(result.w))
            writeRotation(rotation, lane, result);
    }

    ozz::animation::LocalToModelJob toModel;
    toModel.skeleton = &rig.skeleton;
    toModel.input = ozz::make_span(mImpl->poseLocals);
    toModel.output = ozz::make_span(mImpl->modelTransforms);
    if (!toModel.Run())
        return false;

    for (size_t joint = 0; joint < mImpl->modelTransforms.size(); ++joint) {
        glm::mat4 matrix(1.0f);
        float* values = glm::value_ptr(matrix);
        ozz::math::StorePtrU(mImpl->modelTransforms[joint].cols[0], values + 0);
        ozz::math::StorePtrU(mImpl->modelTransforms[joint].cols[1], values + 4);
        ozz::math::StorePtrU(mImpl->modelTransforms[joint].cols[2], values + 8);
        ozz::math::StorePtrU(mImpl->modelTransforms[joint].cols[3], values + 12);
        mImpl->modelMatrices[joint] = finiteMatrix(matrix) ? matrix : glm::mat4(1.0f);
    }
    mImpl->poseReady = true;
    return true;
}

std::span<const glm::mat4> PoseBlender::modelMatrices() const
{
    if (!mImpl || !mImpl->poseReady)
        return {};
    return mImpl->modelMatrices;
}

} // namespace eng::animation
