#include <eng/animation/SkeletalAnimation.h>

#include <eng/Log.h>

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
