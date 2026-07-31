#pragma once
#include <eng/particles/ParticleCollider.h>
#include <eng/particles/ParticleEffectDesc.h>
#include <eng/particles/ParticleTypes.h>

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace eng {

// Piecewise-linear evaluation over the ramp stops. Ogre approximated a ramp with
// a single interpolator or a constant slope; evaluating it here is what lets an
// effect fade in, hold, and fade out in one description.
glm::vec4 evalColourRamp(const std::vector<ColourStop>& ramp, float t,
                         glm::vec4 fallback);
float evalSizeRamp(const std::vector<SizeStop>& ramp, float t);

// DecalRequest and IParticleCollider live in eng/particles/ParticleCollider.h:
// the application has to name the collider to install one, so the seam is
// public even though everything else about the simulation is not.

enum ParticleFlag : uint8_t {
    ParticleFlag_Alive = 1u << 0,
    ParticleFlag_Collide = 1u << 1,
    ParticleFlag_LocalSpace = 1u << 2,
};

// Struct of arrays, fixed capacity, compacted by swap-remove: [0, alive) is the
// live set and nothing after it is ever read. Reserved once at init so a burst
// costs no allocation.
struct ParticlePool {
    std::vector<glm::vec3> pos, vel;
    std::vector<glm::vec4> colour;
    std::vector<float> age, life, size, rot, rotVel, seed;
    std::vector<uint16_t> effect;   // index into the registered effect descs
    std::vector<uint16_t> owner;    // instance slot: ramps, tint, base size
    std::vector<uint8_t> flags;
    uint32_t alive = 0;

    void reserve(size_t capacity);
    size_t capacity() const { return pos.size(); }
    bool full() const { return alive >= pos.size(); }
    uint32_t push();               // returns the new index; capacity() if full
    void swapRemove(uint32_t index);
    void clear() { alive = 0; }
};

// A live spawn: everything that varies per call site rather than per effect. The
// parent transform is supplied by the caller every frame as a plain matrix,
// which is what keeps the sim free of any scene-graph dependency.
struct EffectInstance {
    uint16_t effect = 0;
    ResolvedParticleSpawn resolved;
    glm::mat4 transform{1.0f};
    std::vector<float> accum;      // fractional particles owed, per emitter
    float age = 0.0f;
    float maxLife = 0.0f;          // emission window + longest particle ttl
    uint32_t seed = 1u;
    uint32_t spawnCounter = 0u;
    uint32_t liveParticles = 0u;
    bool oneShot = false;
    bool emitting = false;         // cleared by stop() and by the one-shot window
    bool active = false;           // false once retired *and* drained of particles
};

class ParticleSim {
public:
    void reserve(size_t capacity);
    void setCollider(IParticleCollider* collider) { collider_ = collider; }
    void setRayBudget(uint32_t rays) { rayBudget_ = rays; }
    uint32_t rayBudget() const { return rayBudget_; }
    // Rays actually issued last frame; the panel reports it, tests assert on it.
    uint32_t raysLastFrame() const { return raysLastFrame_; }

    uint16_t registerEffect(const ParticleEffectDesc& desc);
    // Hot-reload: replace a desc in place so live particles pick up the new
    // ramps on their next update instead of being killed.
    void updateEffect(uint16_t effect, const ParticleEffectDesc& desc);
    const ParticleEffectDesc& effectDesc(uint16_t effect) const;
    size_t effectCount() const { return effects_.size(); }

    // Returns an instance slot id, or kInvalidInstance if the effect is unknown.
    uint32_t addInstance(uint16_t effect, const ResolvedParticleSpawn& resolved,
                         const glm::mat4& transform, uint32_t seed);
    void setInstanceTransform(uint32_t instance, const glm::mat4& transform);
    void stopInstance(uint32_t instance);   // stop emitting, let particles age out
    void killInstance(uint32_t instance);   // stop emitting and retire its particles
    bool instanceActive(uint32_t instance) const;
    bool instanceEmitting(uint32_t instance) const;
    const EffectInstance* instance(uint32_t instance) const;

    // Integrate, collide under budget, evaluate ramps, retire, then emit.
    // Collision runs inside integration because it needs the swept segment.
    void update(float dt, std::vector<DecalRequest>& decalsOut);
    void clear();

    // --- renderer read access ---------------------------------------------
    // The batcher walks one effect at a time. indicesForEffect() is rebuilt at
    // the end of every update from the compacted pool, so the ranges are stable
    // for the whole frame the renderer sees.
    const ParticlePool& pool() const { return pool_; }
    const std::vector<uint32_t>& indicesForEffect(uint16_t effect) const;
    uint32_t liveCount() const { return pool_.alive; }
    uint32_t liveCount(uint16_t effect) const;

    static constexpr uint32_t kInvalidInstance = 0xFFFFFFFFu;

private:
    void integrate(float dt, std::vector<DecalRequest>& decalsOut);
    void shade();
    void retireExpired();
    void emit(float dt);
    void emitFrom(EffectInstance& inst, uint32_t instanceIndex, float dt);
    void spawnOne(EffectInstance& inst, uint32_t instanceIndex,
                  const ResolvedParticleEmitter& emitter);
    void rebuildIndices();
    void releaseSlot(uint32_t instance);
    // True when this particle may spend one of the frame's rays.
    bool takeRay();

    ParticlePool pool_;
    std::vector<ParticleEffectDesc> effects_;
    std::vector<EffectInstance> instances_;
    std::vector<uint32_t> freeInstances_;
    std::vector<std::vector<uint32_t>> indices_;
    std::vector<uint32_t> emptyIndices_;
    // Collision scratch, reserved with the pool. Tracing is a second pass over
    // the integrated set so the round-robin cursor can start anywhere without
    // disturbing integration order.
    std::vector<uint32_t> candidates_;
    std::vector<glm::vec3> prevPos_;

    IParticleCollider* collider_ = nullptr;
    uint32_t rayBudget_ = 64;
    uint32_t raysUsed_ = 0;
    uint32_t raysLastFrame_ = 0;
    // Round-robin cursor over the pool so a burst larger than the budget still
    // gets every particle traced eventually, just not every frame.
    uint32_t rayCursor_ = 0;
};

} // namespace eng
