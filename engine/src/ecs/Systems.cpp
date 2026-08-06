#include <eng/ecs/Systems.h>

#include <eng/ecs/World.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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

// The rotation that points -Z along `forward`, with `up` as the reference.
//
// Hand-rolled rather than glm::quatLookAt, which lives in GLM's experimental
// gtx: enabling that for one function would turn on a header set the rest of
// this engine has deliberately stayed off. The basis IS the rotation matrix --
// the same identity Transform.h's decompose() relies on in reverse.
glm::quat lookRotation(const glm::vec3& forward, const glm::vec3& up)
{
    // Third column, because -Z is forward in this renderer's convention.
    const glm::vec3 z = -forward;
    glm::vec3 reference = up;
    // A forward parallel to `up` leaves the cross product at zero and the basis
    // degenerate; any other reference gives the same ring with a different roll,
    // which for a camera looking straight down is the only free choice anyway.
    if (std::abs(glm::dot(glm::normalize(reference), z)) > 0.999f)
        reference = std::abs(z.y) > 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                         : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 x = glm::normalize(glm::cross(reference, z));
    const glm::vec3 y = glm::cross(z, x);
    return glm::normalize(glm::quat_cast(glm::mat3(x, y, z)));
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

void orbitSystem(World& world, float dt)
{
    entt::registry& reg = world.registry();
    for (auto e : reg.view<Orbit, Transform>()) {
        Orbit& orbit = reg.get<Orbit>(e);
        const float axisLength = glm::length(orbit.axis);
        if (axisLength <= 0.0f)
            continue; // a ring with no normal is not a ring
        orbit.travelled += orbit.degreesPerSecond * dt;

        const glm::vec3 axis = orbit.axis / axisLength;
        // Two vectors spanning the ring's plane. Built from the axis rather
        // than authored, so an inclined orbit needs one field and not three:
        // any vector not parallel to the axis will do, and +X only fails when
        // the axis IS +X, which is exactly when +Y does not.
        const glm::vec3 seed = std::abs(axis.x) > 0.9f
                                   ? glm::vec3(0.0f, 1.0f, 0.0f)
                                   : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 u = glm::normalize(glm::cross(seed, axis));
        const glm::vec3 v = glm::cross(axis, u);

        const float angle = glm::radians(orbit.travelled + orbit.phaseDegrees);
        const glm::vec3 offset =
            (u * std::cos(angle) + v * std::sin(angle)) * orbit.radius;

        Transform t = reg.get<Transform>(e);
        t.position = orbit.centre + offset + axis * orbit.height;

        // Facing. Free leaves the rotation exactly as it was, which is what
        // lets Spin own it on the same entity.
        if (orbit.facing != Orbit::Free) {
            // Tangent to the ring, in the direction of travel -- and reversed
            // with the rate, so a negative rate really does run the other way
            // round rather than flying backwards.
            const glm::vec3 tangent =
                (u * -std::sin(angle) + v * std::cos(angle)) *
                (orbit.degreesPerSecond < 0.0f ? -1.0f : 1.0f);
            const glm::vec3 wanted =
                orbit.facing == Orbit::Centre ? (t.position - orbit.centre) * -1.0f
                                              : tangent;
            const float length = glm::length(wanted);
            // Degenerate only at radius zero (nothing to look away from) and
            // for a stationary Travel (no direction to face). Both keep the
            // authored rotation rather than producing a NaN quaternion.
            if (length > 1e-5f) {
                // -Z forward, +Y up: the renderer's convention, so a camera
                // with this component frames what a camera parented to a pivot
                // used to frame by accident.
                t.rotation = lookRotation(wanted / length, axis);
            }
        }
        // Through the World, not the component: an orbiting entity may carry a
        // subtree, and it has to come with it.
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
    // After spin, so an entity carrying both ends the frame where Orbit puts
    // it and turned by whichever of the two owns its facing.
    orbitSystem(world, dt);
    lightAnimationSystem(world, dt);
    // After the three procedural modulators: a clip is the more specific
    // statement about a field, so it gets the last word on any they share. Free
    // on a World that never attached a component table.
    clipSystem(world, dt);
    // Last: an entity in its final frame animates like any other, and anything
    // the systems above would have touched is gone before the frame's sync.
    lifetimeSystem(world, dt);
}

} // namespace eng::ecs
