#include "ParticleSim.h"

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

glm::vec3 finiteOr(glm::vec3 v, glm::vec3 fallback)
{
    return glm::vec3(finiteOr(v.x, fallback.x), finiteOr(v.y, fallback.y),
                     finiteOr(v.z, fallback.z));
}

const ParticleEffectDesc& fallbackDesc()
{
    static const ParticleEffectDesc empty;
    return empty;
}

} // namespace

// --- ramps -----------------------------------------------------------------

glm::vec4 evalColourRamp(const std::vector<ColourStop>& ramp, float t,
                         glm::vec4 fallback)
{
    if (ramp.empty())
        return fallback;
    t = std::clamp(finiteOr(t, 0.0f), 0.0f, 1.0f);
    if (t <= ramp.front().t)
        return ramp.front().rgba;
    for (size_t i = 1; i < ramp.size(); ++i) {
        if (t > ramp[i].t)
            continue;
        const float span = ramp[i].t - ramp[i - 1].t;
        // Coincident stops are a legitimate way to author a hard cut.
        if (!(span > 1e-6f))
            return ramp[i].rgba;
        const float k = (t - ramp[i - 1].t) / span;
        return ramp[i - 1].rgba + (ramp[i].rgba - ramp[i - 1].rgba) * k;
    }
    return ramp.back().rgba;
}

float evalSizeRamp(const std::vector<SizeStop>& ramp, float t)
{
    if (ramp.empty())
        return 1.0f;
    t = std::clamp(finiteOr(t, 0.0f), 0.0f, 1.0f);
    if (t <= ramp.front().t)
        return ramp.front().scale;
    for (size_t i = 1; i < ramp.size(); ++i) {
        if (t > ramp[i].t)
            continue;
        const float span = ramp[i].t - ramp[i - 1].t;
        if (!(span > 1e-6f))
            return ramp[i].scale;
        const float k = (t - ramp[i - 1].t) / span;
        return ramp[i - 1].scale + (ramp[i].scale - ramp[i - 1].scale) * k;
    }
    return ramp.back().scale;
}

// --- pool ------------------------------------------------------------------

void ParticlePool::reserve(size_t capacity)
{
    pos.assign(capacity, glm::vec3(0.0f));
    vel.assign(capacity, glm::vec3(0.0f));
    colour.assign(capacity, glm::vec4(1.0f));
    age.assign(capacity, 0.0f);
    life.assign(capacity, 0.0f);
    size.assign(capacity, 0.0f);
    rot.assign(capacity, 0.0f);
    rotVel.assign(capacity, 0.0f);
    seed.assign(capacity, 0.0f);
    effect.assign(capacity, 0);
    owner.assign(capacity, 0);
    flags.assign(capacity, 0);
    alive = 0;
}

uint32_t ParticlePool::push()
{
    if (full())
        return uint32_t(pos.size());
    return alive++;
}

void ParticlePool::swapRemove(uint32_t index)
{
    if (index >= alive)
        return;
    const uint32_t last = alive - 1;
    if (index != last) {
        pos[index] = pos[last];
        vel[index] = vel[last];
        colour[index] = colour[last];
        age[index] = age[last];
        life[index] = life[last];
        size[index] = size[last];
        rot[index] = rot[last];
        rotVel[index] = rotVel[last];
        seed[index] = seed[last];
        effect[index] = effect[last];
        owner[index] = owner[last];
        flags[index] = flags[last];
    }
    flags[last] = 0;
    alive = last;
}

// --- sim -------------------------------------------------------------------

void ParticleSim::reserve(size_t capacity)
{
    pool_.reserve(capacity);
    candidates_.reserve(capacity);
    prevPos_.reserve(capacity);
    for (auto& list : indices_)
        list.reserve(capacity);
}

uint16_t ParticleSim::registerEffect(const ParticleEffectDesc& desc)
{
    effects_.push_back(desc);
    indices_.emplace_back();
    indices_.back().reserve(pool_.capacity());
    return uint16_t(effects_.size() - 1);
}

void ParticleSim::updateEffect(uint16_t effect, const ParticleEffectDesc& desc)
{
    if (effect < effects_.size())
        effects_[effect] = desc;
}

const ParticleEffectDesc& ParticleSim::effectDesc(uint16_t effect) const
{
    return effect < effects_.size() ? effects_[effect] : fallbackDesc();
}

uint32_t ParticleSim::addInstance(uint16_t effect,
                                  const ResolvedParticleSpawn& resolved,
                                  const glm::mat4& transform, uint32_t seed)
{
    if (effect >= effects_.size())
        return kInvalidInstance;

    uint32_t slot;
    if (!freeInstances_.empty()) {
        slot = freeInstances_.back();
        freeInstances_.pop_back();
    } else {
        // uint16 owner indices cap the number of concurrent instances; refusing
        // is better than aliasing another instance's ramps.
        if (instances_.size() >= 0xFFFFu)
            return kInvalidInstance;
        slot = uint32_t(instances_.size());
        instances_.emplace_back();
    }

    EffectInstance& inst = instances_[slot];
    inst = EffectInstance{};
    inst.effect = effect;
    inst.resolved = resolved;
    inst.transform = transform;
    inst.accum.assign(resolved.emitters.size(), 0.0f);
    inst.seed = seed ? seed : hashParticleSeed(slot + 1u, 0x5EEDu);
    inst.oneShot = !effects_[effect].loop;

    float window = 0.0f;
    for (const ResolvedParticleEmitter& e : resolved.emitters)
        window = std::max(window, finiteOr(e.emissionDuration, 0.0f));
    inst.maxLife = window + std::max(0.0f, finiteOr(resolved.maxTtl, 0.0f));
    inst.emitting = true;
    inst.active = true;
    return slot;
}

void ParticleSim::setInstanceTransform(uint32_t instance,
                                       const glm::mat4& transform)
{
    if (instance < instances_.size() && instances_[instance].active)
        instances_[instance].transform = transform;
}

void ParticleSim::stopInstance(uint32_t instance)
{
    if (instance >= instances_.size())
        return;
    EffectInstance& inst = instances_[instance];
    inst.emitting = false;
    // A stopped instance still owns its transform until its particles die, which
    // is what local-space particles read every frame.
    if (inst.liveParticles == 0)
        releaseSlot(instance);
}

void ParticleSim::killInstance(uint32_t instance)
{
    if (instance >= instances_.size())
        return;
    instances_[instance].emitting = false;
    for (uint32_t i = 0; i < pool_.alive;) {
        if (pool_.owner[i] == uint16_t(instance))
            pool_.swapRemove(i);
        else
            ++i;
    }
    instances_[instance].liveParticles = 0;
    releaseSlot(instance);
}

bool ParticleSim::instanceActive(uint32_t instance) const
{
    return instance < instances_.size() && instances_[instance].active;
}

bool ParticleSim::instanceEmitting(uint32_t instance) const
{
    return instance < instances_.size() && instances_[instance].emitting;
}

const EffectInstance* ParticleSim::instance(uint32_t instance) const
{
    if (instance >= instances_.size() || !instances_[instance].active)
        return nullptr;
    return &instances_[instance];
}

void ParticleSim::releaseSlot(uint32_t instance)
{
    EffectInstance& inst = instances_[instance];
    if (!inst.active)
        return;
    inst.active = false;
    inst.emitting = false;
    freeInstances_.push_back(instance);
}

void ParticleSim::clear()
{
    pool_.clear();
    instances_.clear();
    freeInstances_.clear();
    for (auto& list : indices_)
        list.clear();
    rayCursor_ = 0;
}

bool ParticleSim::takeRay()
{
    if (raysUsed_ >= rayBudget_)
        return false;
    ++raysUsed_;
    return true;
}

void ParticleSim::update(float dt, std::vector<DecalRequest>& decalsOut)
{
    // A non-finite or negative dt would poison every position in the pool, and
    // it happens for real on the first frame after a load stall.
    dt = std::clamp(finiteOr(dt, 0.0f), 0.0f, 0.25f);
    raysUsed_ = 0;

    integrate(dt, decalsOut);
    shade();
    retireExpired();
    emit(dt);
    rebuildIndices();
    raysLastFrame_ = raysUsed_;
}

void ParticleSim::integrate(float dt, std::vector<DecalRequest>& decalsOut)
{
    candidates_.clear();
    prevPos_.clear();

    for (uint32_t i = 0; i < pool_.alive; ++i) {
        const ParticleEffectDesc& desc = effectDesc(pool_.effect[i]);
        glm::vec3 v = pool_.vel[i];
        v += finiteOr(desc.acceleration, glm::vec3(0.0f)) * dt;
        const float drag = std::max(0.0f, finiteOr(desc.drag, 0.0f));
        if (drag > 0.0f) {
            // Exponential damping rather than (1 - drag*dt): the linear form
            // goes negative and flips the velocity at large dt.
            v *= std::exp(-drag * dt);
        }
        pool_.vel[i] = v;

        const glm::vec3 from = pool_.pos[i];
        pool_.pos[i] = from + v * dt;
        pool_.rot[i] += pool_.rotVel[i] * dt;
        pool_.age[i] += dt;

        if ((pool_.flags[i] & ParticleFlag_Collide) != 0 && collider_) {
            candidates_.push_back(i);
            prevPos_.push_back(from);
        }
    }

    if (candidates_.empty())
        return;

    const size_t count = candidates_.size();
    const size_t start = count ? (rayCursor_ % count) : 0;
    for (size_t n = 0; n < count; ++n) {
        if (!takeRay())
            break;
        const size_t c = (start + n) % count;
        const uint32_t i = candidates_[c];
        glm::vec3 hitPos(0.0f);
        glm::vec3 hitNormal(0.0f, 1.0f, 0.0f);
        if (!collider_->sweep(prevPos_[c], pool_.pos[i], hitPos, hitNormal))
            continue;

        const ParticleEffectDesc& desc = effectDesc(pool_.effect[i]);
        const glm::vec3 normal =
            glm::length(hitNormal) > 1e-6f
                ? glm::normalize(finiteOr(hitNormal, glm::vec3(0, 1, 0)))
                : glm::vec3(0.0f, 1.0f, 0.0f);
        switch (desc.collideResponse) {
            case ParticleCollideResponse::Bounce: {
                pool_.pos[i] = hitPos + normal * 1e-3f;
                const glm::vec3 v = pool_.vel[i];
                const glm::vec3 normalPart = normal * glm::dot(v, normal);
                const glm::vec3 tangent = v - normalPart;
                const float restitution =
                    std::clamp(finiteOr(desc.restitution, 0.0f), 0.0f, 1.0f);
                const float friction =
                    std::clamp(finiteOr(desc.friction, 0.0f), 0.0f, 1.0f);
                pool_.vel[i] =
                    tangent * (1.0f - friction) - normalPart * restitution;
                break;
            }
            case ParticleCollideResponse::Decal:
                // An effect with no profile would otherwise mark every surface
                // it touches with nothing, so it just dies.
                if (!desc.decalProfile.empty())
                    decalsOut.push_back(
                        DecalRequest{desc.decalProfile, hitPos, normal});
                pool_.pos[i] = hitPos;
                pool_.age[i] = pool_.life[i];
                break;
            case ParticleCollideResponse::Die:
                pool_.pos[i] = hitPos;
                pool_.age[i] = pool_.life[i];
                break;
            case ParticleCollideResponse::None:
                break;
        }
    }
    rayCursor_ = uint32_t((start + raysUsed_) % count);
}

void ParticleSim::shade()
{
    for (uint32_t i = 0; i < pool_.alive; ++i) {
        const ParticleEffectDesc& desc = effectDesc(pool_.effect[i]);
        const EffectInstance& inst = instances_[pool_.owner[i]];
        const float life = std::max(1e-4f, pool_.life[i]);
        const float t = std::clamp(pool_.age[i] / life, 0.0f, 1.0f);

        // The instance's ramp is already tinted by resolveParticleSpawn, so the
        // per-call colour override costs nothing per frame here.
        const glm::vec4 base =
            inst.resolved.emitters.empty()
                ? glm::vec4(1.0f)
                : inst.resolved.emitters.front().colour;
        glm::vec4 c = evalColourRamp(inst.resolved.colourRamp, t, base);

        // seed carries the particle's stable [-1,1] jitter draw; hue jitter is a
        // multiplicative wobble on rgb so an ember field is not one flat colour.
        const float jitter = pool_.seed[i];
        const float hue = std::max(0.0f, finiteOr(desc.hueJitter, 0.0f));
        if (hue > 0.0f) {
            const float k = 1.0f + jitter * hue;
            c.r *= k;
            c.g *= k;
            c.b *= k;
        }
        pool_.colour[i] = c;

        const float scaleJitter = std::max(0.0f, finiteOr(desc.scaleJitter, 0.0f));
        const float baseSize = 0.5f * (finiteOr(desc.baseWidth, 0.1f) +
                                       finiteOr(desc.baseHeight, 0.1f)) *
                               inst.resolved.options.sizeScale;
        pool_.size[i] = std::max(0.0f, baseSize *
                                           (1.0f + jitter * scaleJitter) *
                                           evalSizeRamp(desc.sizeRamp, t));
    }
}

void ParticleSim::retireExpired()
{
    for (uint32_t i = 0; i < pool_.alive;) {
        if (pool_.age[i] >= pool_.life[i]) {
            EffectInstance& inst = instances_[pool_.owner[i]];
            if (inst.liveParticles > 0)
                --inst.liveParticles;
            pool_.swapRemove(i);
        } else {
            ++i;
        }
    }
}

void ParticleSim::emit(float dt)
{
    for (uint32_t s = 0; s < instances_.size(); ++s) {
        EffectInstance& inst = instances_[s];
        if (!inst.active)
            continue;
        inst.age += dt;
        if (inst.emitting)
            emitFrom(inst, s, dt);
        // Retirement is decided by the resolved window, never by the live count:
        // an off-screen effect may be simulated less often than it emits.
        if (inst.oneShot &&
            particleSystemLifetimeExpired(true, inst.age, inst.maxLife))
            inst.emitting = false;
        if (!inst.emitting && inst.liveParticles == 0)
            releaseSlot(s);
    }
}

void ParticleSim::emitFrom(EffectInstance& inst, uint32_t instanceIndex, float dt)
{
    for (size_t e = 0; e < inst.resolved.emitters.size(); ++e) {
        const ResolvedParticleEmitter& emitter = inst.resolved.emitters[e];
        const float duration = finiteOr(emitter.emissionDuration, 0.0f);
        if (duration > 0.0f && inst.age >= duration)
            continue;
        const float rate = std::max(0.0f, finiteOr(emitter.emissionRate, 0.0f));
        if (rate <= 0.0f)
            continue;

        float& accum = inst.accum[e];
        accum += rate * dt;
        while (accum >= 1.0f) {
            accum -= 1.0f;
            if (pool_.full())
                break;
            spawnOne(inst, instanceIndex, emitter);
        }
        if (pool_.full())
            accum = 0.0f;
    }
}

void ParticleSim::spawnOne(EffectInstance& inst, uint32_t instanceIndex,
                           const ResolvedParticleEmitter& emitter)
{
    const ParticleEffectDesc& desc = effectDesc(inst.effect);
    const uint32_t seed = hashParticleSeed(inst.seed, inst.spawnCounter++);
    const ParticleSpawnSample sample = sampleParticleSpawn(emitter, seed);

    const uint32_t i = pool_.push();
    if (i >= pool_.capacity())
        return;

    glm::vec3 position = sample.position;
    glm::vec3 velocity = sample.velocity;
    if (!desc.localSpace) {
        // World-space particles bake the parent transform once at birth; local
        // space ones stay in the parent's frame and the renderer applies it.
        position = glm::vec3(inst.transform * glm::vec4(position, 1.0f));
        velocity = glm::vec3(inst.transform * glm::vec4(velocity, 0.0f));
    }

    // One extra draw after the spawn sample, so rotation jitter never perturbs
    // the position/velocity sequence the tests pin down.
    ParticleRng rng(hashParticleSeed(seed, 0xA5A5u));
    const float rotJitter = finiteOr(desc.rotationJitterDeg, 0.0f);

    pool_.pos[i] = position;
    pool_.vel[i] = velocity;
    pool_.colour[i] = emitter.colour;
    pool_.age[i] = 0.0f;
    pool_.life[i] = std::max(1e-4f, sample.ttl);
    pool_.size[i] = std::max(0.0f, inst.resolved.defaultWidth);
    pool_.rot[i] = rng.signedUnit() * rotJitter * (kPi / 180.0f);
    pool_.rotVel[i] = 0.0f;
    pool_.seed[i] = rng.signedUnit();
    pool_.effect[i] = inst.effect;
    pool_.owner[i] = uint16_t(instanceIndex);
    uint8_t flags = ParticleFlag_Alive;
    if (desc.collides())
        flags |= ParticleFlag_Collide;
    if (desc.localSpace)
        flags |= ParticleFlag_LocalSpace;
    pool_.flags[i] = flags;
    ++inst.liveParticles;
}

void ParticleSim::rebuildIndices()
{
    for (auto& list : indices_)
        list.clear();
    for (uint32_t i = 0; i < pool_.alive; ++i) {
        const uint16_t fx = pool_.effect[i];
        if (fx < indices_.size())
            indices_[fx].push_back(i);
    }
}

const std::vector<uint32_t>& ParticleSim::indicesForEffect(uint16_t effect) const
{
    if (effect < indices_.size())
        return indices_[effect];
    return emptyIndices_;
}

uint32_t ParticleSim::liveCount(uint16_t effect) const
{
    return uint32_t(indicesForEffect(effect).size());
}

} // namespace eng
