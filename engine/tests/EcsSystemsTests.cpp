#include <eng/ecs/Systems.h>
#include <eng/ecs/World.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace eng;
using namespace eng::ecs;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "EcsSystemsTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    // --- Lifetime: expires, and takes its subtree with it ------------------
    {
        World world;
        const entt::entity bolt = world.create("bolt");
        const entt::entity trail = world.create("trail");
        world.setParent(trail, bolt);
        world.registry().emplace<Lifetime>(bolt, Lifetime{0.25f});

        lifetimeSystem(world, 0.1f);
        require(world.registry().valid(bolt), "not expired yet");
        require(world.registry().get<Lifetime>(bolt).remaining < 0.25f,
                "lifetime counts down");

        lifetimeSystem(world, 0.2f);
        require(!world.registry().valid(bolt), "expired entity is destroyed");
        require(!world.registry().valid(trail),
                "the subtree goes with it -- a trail must not outlive its bolt");
    }

    // A survivor with no Lifetime is untouched, and an expiring sibling does
    // not disturb the view it was found through.
    {
        World world;
        const entt::entity keep = world.create("keep");
        const entt::entity a = world.create("a");
        const entt::entity b = world.create("b");
        world.registry().emplace<Lifetime>(a, Lifetime{0.0f});
        world.registry().emplace<Lifetime>(b, Lifetime{0.0f});
        lifetimeSystem(world, 0.016f);
        require(world.registry().valid(keep), "entities without Lifetime survive");
        require(!world.registry().valid(a) && !world.registry().valid(b),
                "every expired entity in one pass is destroyed");
    }

    // --- Spin: rotates the local transform and dirties the subtree ---------
    {
        World world;
        const entt::entity ring = world.create("ring");
        const entt::entity gem = world.create("gem");
        world.setParent(gem, ring);
        world.registry().emplace<Spin>(ring, Spin{{0.0f, 1.0f, 0.0f}, 90.0f});
        world.updateWorldTransforms(); // start clean

        spinSystem(world, 0.5f); // quarter turn
        const glm::quat r = world.registry().get<Transform>(ring).rotation;
        require(std::abs(glm::degrees(glm::angle(r)) - 45.0f) < 0.01f,
                "spin applies degreesPerSecond * dt");
        require(world.registry().all_of<Dirty>(gem),
                "a spinning parent dirties what is mounted on it");

        // A zero axis is a disabled spin, not a NaN quaternion.
        const entt::entity still = world.create("still");
        world.registry().emplace<Spin>(still, Spin{glm::vec3(0.0f), 90.0f});
        spinSystem(world, 0.5f);
        const glm::quat s = world.registry().get<Transform>(still).rotation;
        require(s.w == 1.0f, "a zero axis leaves the rotation alone");
    }

    // --- LightAnimation: modulates from the authored colour ----------------
    {
        World world;
        LightDesc desc;
        desc.colour = glm::vec3(1.0f, 0.8f, 0.5f);
        const entt::entity torch = spawnLight(world, desc, glm::vec3(0.0f), "torch");
        world.registry().emplace<LightAnimation>(
            torch, LightAnimation{LightAnimation::Flicker, 7.0f, 0.5f, 0.0f, 0.0f});

        float minimum = 10.0f;
        float maximum = -10.0f;
        for (int i = 0; i < 240; ++i) {
            lightAnimationSystem(world, 1.0f / 60.0f);
            const float v = world.registry().get<LightColour>(torch).value.r;
            minimum = std::min(minimum, v);
            maximum = std::max(maximum, v);
        }
        require(maximum <= desc.colour.r + 1e-5f,
                "flicker never brightens past the authored colour");
        require(minimum >= desc.colour.r * 0.5f - 1e-5f,
                "flicker stays within `amount` of it");
        require(maximum - minimum > 0.05f, "flicker actually modulates");
        // The failure this guards is compounding: modulating LightColour in
        // place instead of the authored colour fades a torch out in seconds.
        require(world.registry().get<LightRef>(torch).desc.colour == desc.colour,
                "the authored colour is never overwritten");

        // Deterministic: the same elapsed time gives the same colour.
        World other;
        const entt::entity twin = spawnLight(other, desc, glm::vec3(0.0f), "twin");
        other.registry().emplace<LightAnimation>(
            twin, LightAnimation{LightAnimation::Flicker, 7.0f, 0.5f, 0.0f, 0.0f});
        for (int i = 0; i < 240; ++i)
            lightAnimationSystem(other, 1.0f / 60.0f);
        require(other.registry().get<LightColour>(twin).value ==
                    world.registry().get<LightColour>(torch).value,
                "two runs of the same animation agree");

        // Phase is what stops a room of torches blinking in lockstep.
        World phased;
        const entt::entity offset = spawnLight(phased, desc, glm::vec3(0.0f), "b");
        phased.registry().emplace<LightAnimation>(
            offset, LightAnimation{LightAnimation::Flicker, 7.0f, 0.5f, 3.7f, 0.0f});
        for (int i = 0; i < 240; ++i)
            lightAnimationSystem(phased, 1.0f / 60.0f);
        require(phased.registry().get<LightColour>(offset).value !=
                    world.registry().get<LightColour>(torch).value,
                "phase separates two otherwise identical lights");

        // Steady is a way to switch one off without removing the component.
        World steady;
        const entt::entity plain = spawnLight(steady, desc, glm::vec3(0.0f), "s");
        steady.registry().emplace<LightAnimation>(
            plain, LightAnimation{LightAnimation::Steady, 7.0f, 0.9f, 0.0f, 0.0f});
        lightAnimationSystem(steady, 0.5f);
        require(steady.registry().get<LightColour>(plain).value == desc.colour,
                "Steady leaves the authored colour alone");
    }

    // --- the whole tick runs on a world that has none of them --------------
    {
        World world;
        world.create("plain");
        tickComponentSystems(world, 0.016f);
        require(world.registry().storage<entt::entity>().size() == 1u,
                "a world with no behavioural components is untouched");
    }

    std::cout << "EcsSystemsTests OK\n";
    return 0;
}
