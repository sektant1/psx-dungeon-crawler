#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace eng {

// One colour/size keyframe over a particle's normalised lifetime t in [0,1].
struct ColourStop { float t = 0.0f; glm::vec4 rgba{1.0f}; };
struct SizeStop   { float t = 0.0f; float scale = 1.0f; };

enum class ParticleEmitterShape { Point, Box };

struct ParticleSpawnOptions {
    float sizeScale = 1.0f;
    float amountScale = 1.0f;
    float lifetimeScale = 1.0f;
    float speedScale = 1.0f;
    float radiusScale = 1.0f;
    float emitterRadius = 0.0f;
    glm::vec4 colourTint{1.0f};
    glm::vec3 localOffset{0.0f};
};

// Pure normalization shared by runtime callers and tests. Non-finite values
// return to identity/zero; finite values clamp to their field's safe minimum.
inline ParticleSpawnOptions
sanitizeParticleSpawnOptions(const ParticleSpawnOptions& options)
{
    ParticleSpawnOptions clean = options;
    const auto scale = [](float value) {
        return std::isfinite(value) ? std::max(0.001f, value) : 1.0f;
    };
    const auto nonNegative = [](float value) {
        return std::isfinite(value) ? std::max(0.0f, value) : 1.0f;
    };
    const auto finiteOrZero = [](float value) {
        return std::isfinite(value) ? value : 0.0f;
    };

    clean.sizeScale = scale(clean.sizeScale);
    clean.amountScale = nonNegative(clean.amountScale);
    clean.lifetimeScale = scale(clean.lifetimeScale);
    clean.speedScale = nonNegative(clean.speedScale);
    clean.radiusScale = scale(clean.radiusScale);
    clean.emitterRadius =
        std::isfinite(clean.emitterRadius) ? std::max(0.0f, clean.emitterRadius)
                                           : 0.0f;
    for (int i = 0; i < 4; ++i)
        clean.colourTint[i] = nonNegative(clean.colourTint[i]);
    for (int i = 0; i < 3; ++i)
        clean.localOffset[i] = finiteOrZero(clean.localOffset[i]);
    return clean;
}

struct ParticleEmitterDesc {
    ParticleEmitterShape shape = ParticleEmitterShape::Point;
    glm::vec3 boxSize{1.0f};         // width/height/depth for Box emitters
    glm::vec3 position{0.0f};        // local offset from the system origin
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float angleDegrees = 20.0f;      // cone half-spread
    float emissionRate = 20.0f;      // particles/sec (looping)
    float ttlMin = 0.30f, ttlMax = 0.60f;
    float velocityMin = 0.30f, velocityMax = 0.70f;
    glm::vec4 startColour{1.0f};     // used when colourRamp is empty
};

// Renderer-agnostic description of one particle effect. eng::Particles translates
// this into a pooled Ogre ParticleSystem.
struct ParticleEffectDesc {
    std::string name;                // stable id, e.g. "fireball_trail"
    std::string material;            // existing Ogre material name
    float baseWidth = 0.14f, baseHeight = 0.14f;
    int   quota = 48;                // before quality scaling
    std::vector<ParticleEmitterDesc> emitters;

    std::vector<ColourStop> colourRamp; // -> ColourInterpolator (<=6 stops)
    std::vector<SizeStop>   sizeRamp;    // -> Scaler (start->end slope)

    float rotationJitterDeg = 0.0f;  // per-spawn random initial rotation +/-
    float hueJitter   = 0.0f;        // per-spawn +/- fraction on emitter colour
    float scaleJitter = 0.0f;        // per-spawn +/- fraction on base size

    bool  loop = true;               // true: emit until stopped
    float burstCount = 0.0f;         // one-shot: emit ~this many, then stop
    float qualityWeight = 1.0f;      // 0 = ignore quality, 1 = full scale
    bool  softDepthFade = false;     // Task 8: use the soft-fade material variant
    bool  localSpace = false;        // true: particles follow the parent node
    glm::vec3 acceleration{0.0f};    // constant world/local force (gravity, wind)
};

// One-shot retirement is governed by the independently resolved emission
// window + maximum particle TTL. It must not depend on Ogre's live count:
// non-visible systems can intentionally stop simulation before their particles
// age to zero. Looping effects remain active until explicitly stopped.
inline bool particleSystemLifetimeExpired(bool oneShot, float age,
                                          float maxLife)
{
    return oneShot && age >= maxLife;
}

// Fully resolved, renderer-independent checkout data. Runtime systems consume a
// fresh value for every pool checkout instead of inheriting mutable Ogre state.
struct ResolvedParticleEmitter {
    ParticleEmitterShape shape = ParticleEmitterShape::Point;
    glm::vec3 boxSize{0.0f};
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float angleDegrees = 0.0f;
    float emissionRate = 0.0f;
    float emissionDuration = 0.0f;
    float ttlMin = 0.0f;
    float ttlMax = 0.0f;
    float velocityMin = 0.0f;
    float velocityMax = 0.0f;
    glm::vec4 colour{1.0f};
};

struct ResolvedParticleSpawn {
    ParticleSpawnOptions options;
    float defaultWidth = 0.0f;
    float defaultHeight = 0.0f;
    std::vector<ResolvedParticleEmitter> emitters;
    std::vector<ColourStop> colourRamp;
    float scalerRate = 0.0f;
    float maxTtl = 0.0f;
};

inline ResolvedParticleSpawn resolveParticleSpawn(
    const ParticleEffectDesc& desc, const ParticleSpawnOptions& rawOptions,
    glm::vec3 localPos = glm::vec3(0.0f), float qualityScale = 1.0f)
{
    ResolvedParticleSpawn resolved;
    resolved.options = sanitizeParticleSpawnOptions(rawOptions);
    const ParticleSpawnOptions& options = resolved.options;
    qualityScale = std::isfinite(qualityScale)
        ? std::max(0.0f, qualityScale) : 1.0f;

    const float initialScale =
        desc.sizeRamp.empty() ? 1.0f : desc.sizeRamp.front().scale;
    resolved.defaultWidth =
        desc.baseWidth * initialScale * options.sizeScale;
    resolved.defaultHeight =
        desc.baseHeight * initialScale * options.sizeScale;

    resolved.colourRamp = desc.colourRamp;
    for (auto& stop : resolved.colourRamp)
        stop.rgba *= options.colourTint;

    resolved.emitters.reserve(desc.emitters.size());
    const float emitterCount =
        float(std::max<size_t>(1, desc.emitters.size()));
    constexpr float burstWindow = 0.05f;
    for (const ParticleEmitterDesc& emitter : desc.emitters) {
        ResolvedParticleEmitter value;
        const bool spreadPoint =
            emitter.shape == ParticleEmitterShape::Point &&
            options.emitterRadius > 0.0f;
        value.shape = spreadPoint ? ParticleEmitterShape::Box : emitter.shape;
        if (spreadPoint)
            value.boxSize = glm::vec3(options.emitterRadius * 2.0f);
        else if (emitter.shape == ParticleEmitterShape::Box)
            value.boxSize = emitter.boxSize * options.radiusScale;
        value.position = emitter.position + localPos + options.localOffset;
        value.direction = emitter.direction;
        value.angleDegrees = emitter.angleDegrees;
        if (desc.loop) {
            value.emissionRate =
                emitter.emissionRate * options.amountScale * qualityScale;
        } else {
            const float count =
                desc.burstCount * options.amountScale * qualityScale /
                emitterCount;
            value.emissionRate = count / burstWindow;
            value.emissionDuration = burstWindow;
        }
        value.ttlMin = emitter.ttlMin * options.lifetimeScale;
        value.ttlMax = emitter.ttlMax * options.lifetimeScale;
        value.velocityMin = emitter.velocityMin * options.speedScale;
        value.velocityMax = emitter.velocityMax * options.speedScale;
        value.colour = (resolved.colourRamp.empty()
                            ? emitter.startColour * options.colourTint
                            : resolved.colourRamp.front().rgba);
        resolved.maxTtl = std::max(resolved.maxTtl, value.ttlMax);
        resolved.emitters.push_back(value);
    }

    if (desc.sizeRamp.size() >= 2) {
        const float avgTtl = desc.emitters.empty()
            ? 0.5f * options.lifetimeScale
            : 0.5f *
                  (desc.emitters.front().ttlMin +
                   desc.emitters.front().ttlMax) *
                  options.lifetimeScale;
        const float baseDim =
            0.5f * (desc.baseWidth + desc.baseHeight) * options.sizeScale;
        resolved.scalerRate =
            (desc.sizeRamp.back().scale - desc.sizeRamp.front().scale) *
            baseDim / std::max(0.01f, avgTtl);
    }
    return resolved;
}

} // namespace eng
