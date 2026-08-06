#include <eng/ecs/ComponentRegistry.h>
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

    // --- Orbit: a ring around a point, with no pivot entity ----------------
    {
        World world;
        const entt::entity moon = world.create("moon");
        Orbit orbit;
        orbit.centre = {10.0f, 0.0f, 0.0f};
        orbit.radius = 4.0f;
        orbit.degreesPerSecond = 90.0f; // a lap every four seconds
        world.registry().emplace<Orbit>(moon, orbit);

        orbitSystem(world, 0.0f);
        const glm::vec3 start = world.registry().get<Transform>(moon).position;
        require(std::abs(glm::length(start - orbit.centre) - orbit.radius) < 1e-4f,
                "it sits on the ring from the first tick, not at the centre");
        require(std::abs(start.y - orbit.centre.y) < 1e-4f,
                "a +Y axis puts the ring in the XZ plane");

        // A quarter lap: a quarter of the way round, still on the ring.
        orbitSystem(world, 1.0f);
        const glm::vec3 quarter = world.registry().get<Transform>(moon).position;
        require(std::abs(glm::length(quarter - orbit.centre) - orbit.radius) < 1e-4f,
                "the radius is held all the way round");
        require(glm::length(quarter - start) > 1.0f, "and it actually moved");

        // A full lap returns it to where it started. The property that says the
        // angle is being accumulated rather than drifting.
        orbitSystem(world, 3.0f);
        const glm::vec3 lap = world.registry().get<Transform>(moon).position;
        require(glm::length(lap - start) < 1e-3f, "a full lap closes the ring");

        // Height lifts the ring along the axis without moving what it circles.
        world.registry().get<Orbit>(moon).height = 3.0f;
        orbitSystem(world, 0.0f);
        require(std::abs(world.registry().get<Transform>(moon).position.y -
                         (orbit.centre.y + 3.0f)) < 1e-4f,
                "height raises the ring, and only the ring");

        // A zero axis is a disabled orbit, not a NaN position.
        const entt::entity broken = world.create("broken");
        world.registry().emplace<Orbit>(broken, Orbit{{}, glm::vec3(0.0f), 5.0f});
        orbitSystem(world, 1.0f);
        require(world.registry().get<Transform>(broken).position == glm::vec3(0.0f),
                "a zero axis leaves the entity alone");
    }

    // --- Orbit facing, and how it shares an entity with Spin ---------------
    {
        World world;
        const entt::entity camera = world.create("camera");
        Orbit orbit;
        orbit.radius = 5.0f;
        orbit.degreesPerSecond = 45.0f;
        orbit.facing = Orbit::Centre;
        world.registry().emplace<Orbit>(camera, orbit);

        orbitSystem(world, 0.7f);
        const Transform& t = world.registry().get<Transform>(camera);
        // -Z forward: the camera's forward must point back at what it circles.
        const glm::vec3 forward = t.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 toCentre = glm::normalize(orbit.centre - t.position);
        require(glm::dot(forward, toCentre) > 0.999f,
                "facing Centre aims the entity at what it circles -- which is "
                "what a camera parented to a pivot used to get by accident");

        // Free leaves the rotation to whatever else owns it. That is the whole
        // reason a moon can carry Orbit and Spin at once.
        World moonWorld;
        const entt::entity moon = moonWorld.create("moon");
        Orbit free;
        free.radius = 4.0f;
        free.degreesPerSecond = 30.0f;
        moonWorld.registry().emplace<Orbit>(moon, free);
        moonWorld.registry().emplace<Spin>(moon, Spin{{0.0f, 1.0f, 0.0f}, 180.0f});
        tickComponentSystems(moonWorld, 0.5f);
        const Transform& m = moonWorld.registry().get<Transform>(moon);
        require(std::abs(glm::length(m.position - free.centre) - free.radius) < 1e-4f,
                "Orbit still placed it on the ring");
        require(std::abs(glm::degrees(glm::angle(m.rotation)) - 90.0f) < 0.1f,
                "and Spin still owns its rotation -- the order in "
                "tickComponentSystems is what makes both true at once");
    }

    // --- the whole tick runs on a world that has none of them --------------
    {
        World world;
        world.create("plain");
        tickComponentSystems(world, 0.016f);
        require(world.registry().storage<entt::entity>().size() == 1u,
                "a world with no behavioural components is untouched");
    }

    // --- Clip: a short animation over a reflected field --------------------
    //
    // The claim under test is the one the whole design rests on: a clip names a
    // component and a field as *strings* and drives them through the registry,
    // so a field is animatable the day it is reflected and this file needs no
    // per-component code.
    ComponentRegistry types;
    registerEngineComponents(types);

    // A door that slides up over 0.8 s and holds.
    {
        World world;
        world.setComponentTypes(&types);
        const entt::entity door = world.create("door");
        world.registry().emplace<Transform>(door, Transform{});

        Clip clip;
        clip.duration = 0.8f;
        clip.mode = ClipMode::Once;
        ClipTrack track;
        track.component = "Transform";
        track.field = "position";
        track.ease = ClipEase::Linear;
        track.keys = {{0.0f, {0.0f, 0.0f, 0.0f}}, {0.8f, {0.0f, 3.2f, 0.0f}}};
        clip.tracks.push_back(track);
        world.registry().emplace<Clip>(door, clip);

        clipSystem(world, 0.4f);
        require(std::abs(world.registry().get<Transform>(door).position.y - 1.6f) < 1e-3f,
                "halfway through a linear track is halfway up");
        require(world.registry().all_of<Dirty>(door),
                "a clip that moved a Transform must tag it -- the hierarchy "
                "resolve is driven by Dirty, not by comparing poses");

        clipSystem(world, 0.4f);
        require(std::abs(world.registry().get<Transform>(door).position.y - 3.2f) < 1e-3f,
                "Once reaches the last key exactly");
        require(!world.registry().get<Clip>(door).playing, "and stops there");

        clipSystem(world, 10.0f);
        require(std::abs(world.registry().get<Transform>(door).position.y - 3.2f) < 1e-3f,
                "a stopped Once clip holds its last pose rather than drifting");
        require(world.registry().get<Clip>(door).finished,
                "and `finished` stays latched -- a door that has finished "
                "opening is a thing gameplay asks about, so the flag has to "
                "survive the frame that set it");

        // A finished clip costs nothing per frame. The bug this pins: the pose
        // was re-applied every tick, which re-tagged the Transform Dirty and
        // kept the hierarchy resolving that branch forever for something that
        // had not moved since it stopped.
        world.registry().remove<Dirty>(door);
        clipSystem(world, 0.016f);
        require(!world.registry().all_of<Dirty>(door),
                "a stopped clip whose playhead has not moved re-poses nothing");

        // ...but a SCRUB does re-pose. Nothing tells the system the playhead
        // moved on a paused clip, so it compares against the time it last
        // applied rather than testing `playing`.
        world.registry().get<Clip>(door).time = 0.4f;
        clipSystem(world, 0.0f);
        require(std::abs(world.registry().get<Transform>(door).position.y - 1.6f) < 1e-3f,
                "seeking a paused clip re-poses it -- what the Timeline's "
                "scrubbing needs, and what testing `playing` could not do");
    }

    // Loop wraps with fmod rather than by subtracting, so a frame far longer
    // than the clip (a level load, a breakpoint) leaves the playhead in range
    // instead of stuck past the end.
    {
        World world;
        world.setComponentTypes(&types);
        const entt::entity e = world.create("pulse");
        world.registry().emplace<Transform>(e, Transform{});

        Clip clip;
        clip.duration = 1.0f;
        clip.mode = ClipMode::Loop;
        ClipTrack track;
        track.component = "Transform";
        track.field = "position";
        track.ease = ClipEase::Linear;
        track.keys = {{0.0f, {0.0f, 0.0f, 0.0f}}, {1.0f, {0.0f, 10.0f, 0.0f}}};
        clip.tracks.push_back(track);
        world.registry().emplace<Clip>(e, clip);

        clipSystem(world, 7.25f);
        const float t = world.registry().get<Clip>(e).time;
        require(t >= 0.0f && t < 1.0f, "a seven-second frame wraps into range");
        require(std::abs(t - 0.25f) < 1e-3f, "and lands where fmod says");
    }

    // A track naming a component the entity does not carry, or a field that
    // does not exist, is inert -- not a crash. Both are one typo away in a
    // hand-edited scene, and the failure has to be survivable.
    {
        World world;
        world.setComponentTypes(&types);
        const entt::entity e = world.create("typo");
        world.registry().emplace<Transform>(e, Transform{});

        Clip clip;
        ClipTrack absent;    // the component exists, the entity lacks it
        absent.component = "ShaderParams";
        absent.field = "tint";
        absent.keys = {{0.0f, {1.0f, 0.0f, 0.0f}}};
        ClipTrack misspelled;
        misspelled.component = "Transform";
        misspelled.field = "postion";
        misspelled.keys = {{0.0f, {0.0f, 5.0f, 0.0f}}};
        clip.tracks = {absent, misspelled};
        world.registry().emplace<Clip>(e, clip);

        clipSystem(world, 0.5f);
        require(world.registry().get<Transform>(e).position.y == 0.0f,
                "a misspelled field animates nothing rather than something else");
        require(world.registry().valid(e), "and the entity survives");
    }

    // The generality claim, stated as a test: a component this file never
    // mentions by type animates because it is reflected. ShaderParams::tint is
    // a Colour, and nothing in ClipSystem.cpp knows ShaderParams exists.
    {
        World world;
        world.setComponentTypes(&types);
        const entt::entity e = world.create("glow");
        world.registry().emplace<ShaderParams>(e, ShaderParams{});

        Clip clip;
        clip.duration = 1.0f;
        ClipTrack track;
        track.component = "ShaderParams";
        track.field = "tint";
        track.ease = ClipEase::Linear;
        track.keys = {{0.0f, {0.0f, 0.0f, 0.0f}}, {1.0f, {1.0f, 0.5f, 0.0f}}};
        clip.tracks.push_back(track);
        world.registry().emplace<Clip>(e, clip);

        clipSystem(world, 1.0f);
        const glm::vec3 tint = world.registry().get<ShaderParams>(e).tint;
        require(std::abs(tint.r - 1.0f) < 1e-3f && std::abs(tint.g - 0.5f) < 1e-3f,
                "a reflected Colour on a component ClipSystem never heard of");
    }

    // autoplay=false stays put until something sets playing, so a clip a lever
    // is supposed to start does not play itself on load.
    {
        World world;
        world.setComponentTypes(&types);
        const entt::entity e = world.create("waiting");
        world.registry().emplace<Transform>(e, Transform{});

        Clip clip;
        clip.autoplay = false;
        ClipTrack track;
        track.component = "Transform";
        track.field = "position";
        track.keys = {{0.0f, {0.0f, 0.0f, 0.0f}}, {1.0f, {0.0f, 4.0f, 0.0f}}};
        clip.tracks.push_back(track);
        world.registry().emplace<Clip>(e, clip);

        clipSystem(world, 0.5f);
        require(world.registry().get<Transform>(e).position.y == 0.0f,
                "autoplay=false does not play");
        world.registry().get<Clip>(e).playing = true;
        clipSystem(world, 0.5f);
        require(world.registry().get<Transform>(e).position.y > 0.0f,
                "and plays once something starts it");
    }

    // A World with no component table runs the tick without a clip doing
    // anything -- what keeps the headless combat sim and the map tests free of
    // the registry.
    {
        World world;
        const entt::entity e = world.create("orphan");
        world.registry().emplace<Transform>(e, Transform{});
        Clip clip;
        ClipTrack track;
        track.component = "Transform";
        track.field = "position";
        track.keys = {{0.0f, {0.0f, 0.0f, 0.0f}}, {1.0f, {0.0f, 9.0f, 0.0f}}};
        clip.tracks.push_back(track);
        world.registry().emplace<Clip>(e, clip);

        tickComponentSystems(world, 0.5f);
        require(world.registry().get<Transform>(e).position.y == 0.0f,
                "no table, no name resolution, no animation, no crash");
    }

    std::cout << "EcsSystemsTests OK\n";
    return 0;
}
