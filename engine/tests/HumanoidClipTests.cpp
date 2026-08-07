// Does the shipped humanoid rig actually animate?
//
// Written because the actors in game render in what looks like their bind pose
// -- arms splayed out sideways, legs straight -- while walking and attacking,
// and there was no test anywhere that would have caught it. Nothing asserted
// that a cooked clip differs from the rest pose, so every failure mode from
// "the cook produced empty clips" to "the blender leans on rest" looked the
// same from outside: a mannequin standing in an A-pose.
//
// Headless on purpose: AnimationRig::load and PoseBlender need no renderer, so
// this isolates the ASSET and the BLENDER from anything the game does with
// them. If this passes and the game still shows a bind pose, the fault is in
// the animator's layer weights, not in the clips.

#include <eng/animation/SkeletalAnimation.h>
#include <eng/assets/AssetRoot.h>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void require(bool condition, const std::string& what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++gFailures;
    }
}

// How far apart two poses are, as the largest joint translation difference in
// model space. Translation rather than rotation because it is what a viewer
// actually sees: a hand that ends up somewhere else.
float poseDistance(std::span<const glm::mat4> a, std::span<const glm::mat4> b)
{
    if (a.size() != b.size() || a.empty())
        return -1.0f;
    float worst = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        const glm::vec3 pa(a[i][3]);
        const glm::vec3 pb(b[i][3]);
        worst = std::max(worst, glm::length(pa - pb));
    }
    return worst;
}

std::vector<glm::mat4> copyOf(std::span<const glm::mat4> matrices)
{
    return std::vector<glm::mat4>(matrices.begin(), matrices.end());
}

} // namespace

int main()
{
    if (!eng::assets::init() || !eng::assets::mount("game")) {
        std::fprintf(stderr, "SKIP: no content root\n");
        return EXIT_SUCCESS;
    }

    const std::string skeleton =
        eng::assets::resolve("animations/actors/humanoid/humanoid_rig.skeleton.ozz")
            .string();
    const std::string clips =
        eng::assets::resolve("animations/actors/humanoid").string();

    std::string error;
    auto rig = eng::animation::AnimationRig::load(skeleton, clips, &error);
    if (!rig || !rig->valid()) {
        std::fprintf(stderr, "FAIL: humanoid rig did not load: %s\n", error.c_str());
        return EXIT_FAILURE;
    }

    require(rig->jointCount() > 0, "the skeleton has joints");
    require(rig->hasClip("walk_f"), "the forward walk was cooked");
    require(rig->clipDuration("walk_f") > 0.0f, "walk_f has a duration");

    eng::animation::PoseBlender blender;
    require(blender.setRig(rig), "the blender accepts the rig");
    if (!blender.valid())
        return EXIT_FAILURE;

    // The rest pose: no layers at all.
    require(blender.evaluate({}), "an empty layer set evaluates");
    const std::vector<glm::mat4> rest = copyOf(blender.modelMatrices());
    require(!rest.empty(), "the rest pose has matrices");

    const float duration = rig->clipDuration("walk_f");

    // 1. A clip at FULL weight must not be the rest pose. This is the check
    //    that fails if the cook produced empty clips, or if the authored poses
    //    never made it through gltf2ozz.
    eng::animation::PoseLayer walk;
    walk.clip = "walk_f";
    walk.weight = 1.0f;
    walk.time = 0.0f;
    require(blender.evaluate({&walk, 1}), "walk_f evaluates");
    const std::vector<glm::mat4> contact = copyOf(blender.modelMatrices());
    const float fromRest = poseDistance(rest, contact);
    require(fromRest > 0.02f,
            "walk_f at full weight differs from the rest pose (largest joint "
            "moved " + std::to_string(fromRest) + " m; a cooked walk that "
            "equals rest means the clip carries no motion)");

    // 2. Two phases of the same cycle must differ, or the clip is a single
    //    held pose rather than a cycle.
    walk.time = duration * 0.5f;
    require(blender.evaluate({&walk, 1}), "walk_f evaluates at mid-cycle");
    const std::vector<glm::mat4> opposite = copyOf(blender.modelMatrices());
    const float acrossCycle = poseDistance(contact, opposite);
    require(acrossCycle > 0.05f,
            "the two contacts of the walk differ (largest joint moved " +
                std::to_string(acrossCycle) + " m; opposite feet should be "
                "far apart)");

    // 3. A SINGLE layer is normalised back to full strength by ozz, whatever
    //    weight it carries -- so an animator cannot dial a lone clip down
    //    toward rest by lowering its weight. This is not what the header's
    //    "weights are normalised against the rest pose" wording suggests on a
    //    first reading, and getting it backwards is an easy way to write an
    //    animator whose blends do nothing.
    walk.time = 0.0f;
    walk.weight = 0.5f;
    require(blender.evaluate({&walk, 1}), "walk_f evaluates at half weight");
    const std::vector<glm::mat4> half = copyOf(blender.modelMatrices());
    const float halfFromRest = poseDistance(rest, half);
    require(std::fabs(halfFromRest - fromRest) < 1e-4f,
            "one layer is normalised to full strength regardless of its "
            "weight (rest-distance at w=0.5 was " +
                std::to_string(halfFromRest) + " vs " +
                std::to_string(fromRest) + " at w=1)");

    // 4. Idle must not be the rest pose either. The mannequin's modelled
    //    A-pose IS the rest pose, so an idle that equals rest is exactly what
    //    "the arms are splayed out sideways" looks like in game.
    if (rig->hasClip("idle")) {
        eng::animation::PoseLayer idle;
        idle.clip = "idle";
        idle.weight = 1.0f;
        idle.time = 0.0f;
        require(blender.evaluate({&idle, 1}), "idle evaluates");
        const float idleFromRest = poseDistance(rest, copyOf(blender.modelMatrices()));
        require(idleFromRest > 0.02f,
                "idle differs from the bind pose (largest joint moved " +
                    std::to_string(idleFromRest) +
                    " m; an idle equal to rest renders as the A-pose the mesh "
                    "was modelled in)");
    }

    // 5. Where the hands actually hang.
    //
    // Every clip is layered on the authoring script's BASE pose, so a BASE
    // that does not fully undo the mannequin's modelled A-pose splays the arms
    // in ALL of them at once -- which is exactly what shipped: the hand sat
    // 0.39 m off the centreline in idle and in every walk, against roughly
    // 0.25 m for a 1.8 m figure with its arms down, and the actors read as
    // mannequins with their arms out. Nothing caught it, because every other
    // check here passes on a splayed pose that is otherwise animating fine.
    //
    // The bound is deliberately loose. This asserts "the arms hang beside the
    // body", not a particular styling.
    {
        const auto handOffset = [&](const char* clip, float t) {
            eng::animation::PoseLayer layer;
            layer.clip = clip;
            layer.weight = 1.0f;
            layer.time = t;
            blender.evaluate({&layer, 1});
            const auto matrices = blender.modelMatrices();
            const int hand = rig->jointIndex("hand.L");
            const int hips = rig->jointIndex("hips");
            if (hand < 0 || hips < 0)
                return -1.0f;
            return std::fabs(matrices[hand][3][0] - matrices[hips][3][0]);
        };

        const float restHand = std::fabs(rest[rig->jointIndex("hand.L")][3][0] -
                                         rest[rig->jointIndex("hips")][3][0]);
        require(restHand > 0.5f,
                "the mesh really is modelled in an A-pose (rest hand is " +
                    std::to_string(restHand) + " m out), which is why BASE has "
                    "to bring the arms down");

        for (const char* clip : {"idle", "walk_f", "run_f"}) {
            if (!rig->hasClip(clip))
                continue;
            const float offset = handOffset(clip, 0.0f);
            require(offset > 0.0f && offset < 0.36f,
                    std::string("in '") + clip + "' the hand hangs beside the "
                    "body rather than splayed out (" + std::to_string(offset) +
                    " m from the centreline; the A-pose is " +
                    std::to_string(restHand) + " m)");
        }
    }

    if (gFailures == 0)
        std::printf("humanoid_clip: rig has %d joints, %zu clips; walk_f moves "
                    "%.3f m from rest and %.3f m across the cycle\n",
                    rig->jointCount(), rig->clipNames().size(), fromRest,
                    acrossCycle);
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
