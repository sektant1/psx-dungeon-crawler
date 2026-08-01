#include <eng/ecs/Systems.h>

#include <eng/ecs/World.h>

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <vector>

namespace eng::ecs {

namespace {

// Deterministic value noise in [0,1). Hashes the integer part of `t` and
// smooth-steps between neighbours, so the same time always yields the same
// number and a flame looks the same in a capture as it did on screen.
//
// A global RNG would be a frame-order dependency: two torches would light each
// other differently depending on which the view visited first.
float valueNoise(float t)
{
    const auto hash = [](uint32_t x) {
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return float(x & 0xFFFFFFu) / float(0x1000000u);
    };
    const float floored = std::floor(t);
    const auto cell = uint32_t(int32_t(floored));
    const float f = t - floored;
    const float smooth = f * f * (3.0f - 2.0f * f); // smoothstep
    return hash(cell) + (hash(cell + 1u) - hash(cell)) * smooth;
}

} // namespace

void spinSystem(World& world, float dt)
{
    entt::registry& reg = world.registry();
    for (auto e : reg.view<Spin, Transform>()) {
        const Spin& spin = reg.get<Spin>(e);
        const float length = glm::length(spin.axis);
        if (length <= 0.0f || spin.degreesPerSecond == 0.0f)
            continue;
        const float radians = glm::radians(spin.degreesPerSecond) * dt;
        Transform t = reg.get<Transform>(e);
        t.rotation = glm::normalize(
            t.rotation * glm::angleAxis(radians, spin.axis / length));
        // Through the World, not the component: the subtree has to be dirtied
        // or a socket could spin without anything mounted on it following.
        world.setLocalTransform(e, t);
    }
}

void lightAnimationSystem(World& world, float dt)
{
    entt::registry& reg = world.registry();
    for (auto e : reg.view<LightAnimation, LightRef>()) {
        LightAnimation& anim = reg.get<LightAnimation>(e);
        anim.time += dt;

        const float phase = anim.time * anim.speed + anim.phase;
        float factor = 1.0f;
        switch (anim.mode) {
        case LightAnimation::Flicker:
            // Noise pulls *down* only. A flame that brightens above its
            // authored value blows out the level's exposure, and the authored
            // colour is the one the palette was graded against.
            factor = 1.0f - anim.amount * valueNoise(phase);
            break;
        case LightAnimation::Pulse:
            factor = 1.0f - anim.amount *
                                0.5f * (1.0f - std::cos(phase * glm::two_pi<float>()));
            break;
        case LightAnimation::Steady:
        default:
            break;
        }

        // The authored colour is the source; LightColour is the frame's result.
        // Modulating LightColour in place would compound every frame and fade
        // the light to black in a couple of seconds.
        const glm::vec3 base = reg.get<LightRef>(e).desc.colour;
        reg.get_or_emplace<LightColour>(e).value = base * factor;
    }
}

void lifetimeSystem(World& world, float dt)
{
    entt::registry& reg = world.registry();
    // Collected first: destroying inside the view would invalidate it, and a
    // subtree destroy takes entities the same view is still holding.
    std::vector<entt::entity> expired;
    for (auto e : reg.view<Lifetime>()) {
        Lifetime& life = reg.get<Lifetime>(e);
        life.remaining -= dt;
        if (life.remaining <= 0.0f)
            expired.push_back(e);
    }
    for (entt::entity e : expired)
        world.destroyHierarchy(e);
}

void tickComponentSystems(World& world, float dt)
{
    spinSystem(world, dt);
    lightAnimationSystem(world, dt);
    // Last: an entity in its final frame animates like any other, and anything
    // the systems above would have touched is gone before the frame's sync.
    lifetimeSystem(world, dt);
}

} // namespace eng::ecs
