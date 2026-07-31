#include "ParticleEmitters.h"

#include <algorithm>
#include <cmath>

namespace eng {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float finiteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

// Any axis orthogonal to `n`. Picking the smallest component to cross against
// avoids the degenerate case where the helper axis is parallel to `n`.
glm::vec3 anyPerpendicular(glm::vec3 n)
{
    const glm::vec3 helper = (std::fabs(n.x) < 0.9f) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                     : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 t = glm::cross(n, helper);
    const float len = glm::length(t);
    if (!(len > 1e-6f))
        return glm::vec3(1.0f, 0.0f, 0.0f);
    return t / len;
}

} // namespace

uint32_t ParticleRng::nextU32()
{
    // xorshift32: one multiply short of PCG's quality, but it is exactly
    // reproducible across compilers, which is what the tests care about.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float ParticleRng::next01()
{
    return float(nextU32() >> 8) * (1.0f / 16777216.0f);
}

float ParticleRng::range(float lo, float hi)
{
    lo = finiteOr(lo, 0.0f);
    hi = finiteOr(hi, lo);
    if (hi <= lo)
        return lo;
    return lo + (hi - lo) * next01();
}

float ParticleRng::signedUnit()
{
    return next01() * 2.0f - 1.0f;
}

uint32_t hashParticleSeed(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9E3779B1u + b * 0x85EBCA77u + 0x165667B1u;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    h *= 0x27D4EB2Fu;
    h ^= h >> 16;
    return h ? h : 1u;
}

glm::vec3 sampleEmitterPosition(const ResolvedParticleEmitter& emitter,
                                ParticleRng& rng)
{
    glm::vec3 base(finiteOr(emitter.position.x, 0.0f),
                   finiteOr(emitter.position.y, 0.0f),
                   finiteOr(emitter.position.z, 0.0f));
    if (emitter.shape != ParticleEmitterShape::Box)
        return base;

    glm::vec3 half(std::max(0.0f, finiteOr(emitter.boxSize.x, 0.0f)),
                   std::max(0.0f, finiteOr(emitter.boxSize.y, 0.0f)),
                   std::max(0.0f, finiteOr(emitter.boxSize.z, 0.0f)));
    half *= 0.5f;
    return base + glm::vec3(rng.signedUnit() * half.x,
                            rng.signedUnit() * half.y,
                            rng.signedUnit() * half.z);
}

glm::vec3 sampleConeDirection(glm::vec3 direction, float angleDegrees,
                              ParticleRng& rng)
{
    glm::vec3 axis(finiteOr(direction.x, 0.0f), finiteOr(direction.y, 1.0f),
                   finiteOr(direction.z, 0.0f));
    const float len = glm::length(axis);
    axis = (len > 1e-6f) ? axis / len : glm::vec3(0.0f, 1.0f, 0.0f);

    const float half = std::clamp(finiteOr(angleDegrees, 0.0f), 0.0f, 180.0f);
    if (half <= 0.0f)
        return axis;

    const float theta = half * (kPi / 180.0f) * rng.next01();
    const float phi = 2.0f * kPi * rng.next01();
    const glm::vec3 u = anyPerpendicular(axis);
    const glm::vec3 v = glm::cross(axis, u);
    const glm::vec3 radial = u * std::cos(phi) + v * std::sin(phi);
    return glm::normalize(axis * std::cos(theta) + radial * std::sin(theta));
}

ParticleSpawnSample sampleParticleSpawn(const ResolvedParticleEmitter& emitter,
                                        uint32_t seed)
{
    // Draw order is part of the contract: position, direction, speed, ttl.
    // Changing it renumbers every recorded burst.
    ParticleRng rng(seed);
    ParticleSpawnSample sample;
    sample.position = sampleEmitterPosition(emitter, rng);
    const glm::vec3 dir =
        sampleConeDirection(emitter.direction, emitter.angleDegrees, rng);
    const float speed =
        std::max(0.0f, rng.range(emitter.velocityMin, emitter.velocityMax));
    sample.velocity = dir * speed;
    sample.ttl = std::max(0.0f, rng.range(emitter.ttlMin, emitter.ttlMax));
    return sample;
}

} // namespace eng
