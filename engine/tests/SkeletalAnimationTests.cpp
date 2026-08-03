#include <eng/animation/SkeletalAnimation.h>

#include "render/SkinnedAssimpLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "SkeletalAnimationTests: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool finite(const glm::mat4& value)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(value[column][row]))
                return false;
    return true;
}

} // namespace

int main()
{
    const std::filesystem::path directory =
        std::filesystem::path(PROJECT_SOURCE_DIR) /
        "assets/animations/viewmodels/arms";
    std::string error;
    const auto rig = eng::animation::AnimationRig::load(
        (directory / "arms_rig.skeleton.ozz").string(), directory.string(),
        &error);
    require(bool(rig), error.c_str());
    require(rig->jointCount() == 54,
            "benchmark hands skeleton changed joint count");
    require(rig->clipNames().size() == 18,
            "benchmark hands rig did not load all authored clips");
    require(rig->hasClip("finger_gun_fire") &&
                rig->hasClip("knife_hit_01") && rig->hasClip("guard_idle"),
            "required FPS hand clips are missing");
    require(rig->jointIndex("f_index.03.R") >= 0 &&
                rig->jointIndex("missing_joint") == -1,
            "named animation socket lookup failed");

    eng::detail::ImportedSkinnedModel model;
    require(eng::detail::importSkinnedModel(
                (std::filesystem::path(PROJECT_SOURCE_DIR) /
                 "assets/meshes/viewmodels/arms_rig.glb"),
                rig->jointNames(), model, error),
            error.c_str());
    require(model.submeshes.size() == 1,
            "benchmark rig should contain one deforming mesh");
    require(model.submeshes[0].vertices.size() == 758 &&
                model.submeshes[0].indices.size() == 3u * 1176u,
            "benchmark skin geometry changed unexpectedly");
    for (const eng::detail::ImportedSkinnedVertex& vertex :
         model.submeshes[0].vertices) {
        float weight = 0.0f;
        for (size_t influence = 0; influence < 4; ++influence) {
            weight += vertex.weights[influence];
            require(vertex.joints[influence] < rig->jointCount(),
                    "skin influence references unknown joint");
        }
        require(std::abs(weight - 1.0f) < 0.0001f,
                "skin influences are not normalized");
    }

    eng::animation::SkeletalAnimator animator;
    require(animator.setRig(rig), "animator rejected valid rig");
    // Bind-pose reconstruction catches matrix transpose, source/root-space,
    // and joint-remap regressions that finite-pose tests cannot see.
    float maxBindError = 0.0f;
    for (const eng::detail::ImportedSkinnedVertex& vertex :
         model.submeshes[0].vertices) {
        glm::vec4 reconstructed(0.0f);
        for (size_t influence = 0; influence < 4; ++influence) {
            const uint16_t joint = vertex.joints[influence];
            reconstructed +=
                (animator.modelMatrices()[joint] *
                 model.submeshes[0].inverseBindPoses[joint] *
                 glm::vec4(vertex.position, 1.0f)) *
                vertex.weights[influence];
        }
        maxBindError = std::max(
            maxBindError,
            glm::length(glm::vec3(reconstructed) - vertex.position));
    }
    require(maxBindError < 0.001f,
            "rest pose does not reconstruct source bind geometry");
    eng::animation::AnimationPlayOptions idle;
    idle.fadeSeconds = 0.0f;
    require(animator.play("finger_gun_idle", idle), "idle clip did not start");
    require(animator.update(0.0f), "idle clip did not sample at t=0");
    require(animator.modelMatrices().size() == size_t(rig->jointCount()),
            "animator emitted wrong matrix count");
    for (const glm::mat4& matrix : animator.modelMatrices())
        require(finite(matrix), "animator emitted non-finite matrix");

    eng::animation::AnimationPlayOptions fire;
    fire.fadeSeconds = 0.04f;
    fire.loop = false;
    fire.returnClip = "finger_gun_idle";
    fire.returnFadeSeconds = 0.08f;
    require(animator.play("finger_gun_fire", fire),
            "one-shot clip did not start");
    require(animator.update(0.02f), "crossfade sampling failed");
    require(animator.currentClip() == "finger_gun_fire" &&
                animator.normalizedTime() > 0.0f,
            "one-shot playback state did not advance");
    require(animator.update(rig->clipDuration("finger_gun_fire")),
            "one-shot endpoint sampling failed");
    require(animator.currentClip() == "finger_gun_idle",
            "one-shot did not return to authored idle");

    require(!animator.play("missing_clip"),
            "animator accepted a missing clip");
    std::cout << "SkeletalAnimationTests OK\n";
    return EXIT_SUCCESS;
}
