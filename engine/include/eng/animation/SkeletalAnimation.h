#pragma once

#include <glm/glm.hpp>

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

} // namespace eng::animation
