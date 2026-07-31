#pragma once
#include <eng/particles/ParticleEffectDesc.h>

#include <glm/glm.hpp>
#include <cstdint>

namespace eng {

// Emission is seeded rather than drawn from a shared global generator so that a
// replay, a headless test, and the live game produce byte-identical bursts. The
// caller owns the seed; the sim derives one per particle from the instance seed
// and a monotonic spawn counter.
struct ParticleRng {
    uint32_t state = 1u;

    explicit ParticleRng(uint32_t seed) : state(seed ? seed : 1u) {}

    uint32_t nextU32();
    float next01();                       // [0,1)
    float range(float lo, float hi);      // lo when the range is degenerate
    float signedUnit();                   // [-1,1)
};

// Cheap integer avalanche. Used to decorrelate the per-particle seeds derived
// from (instance seed, spawn index) so consecutive particles in one burst do not
// walk the same sequence one step apart.
uint32_t hashParticleSeed(uint32_t a, uint32_t b);

// One resolved spawn, still in the emitter's local frame. The sim applies the
// instance transform afterwards, which is what keeps local-space effects and
// world-space effects on the same code path.
struct ParticleSpawnSample {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float ttl = 0.0f;
};

// Uniform inside the shape: the emitter origin for Point, the box interior for
// Box. resolveParticleSpawn() already turned an `emitterRadius` Point into a Box,
// so no special case for spread points is needed here.
glm::vec3 sampleEmitterPosition(const ResolvedParticleEmitter& emitter,
                                ParticleRng& rng);

// Direction inside a cone of half-angle `angleDegrees` around `direction`.
// Sampling the polar angle directly (rather than solid-angle uniform) matches
// what the old Ogre emitter did and keeps existing effects looking the same.
glm::vec3 sampleConeDirection(glm::vec3 direction, float angleDegrees,
                              ParticleRng& rng);

ParticleSpawnSample sampleParticleSpawn(const ResolvedParticleEmitter& emitter,
                                        uint32_t seed);

} // namespace eng
