#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace eng {

// One colour/size keyframe over a particle's normalised lifetime t in [0,1].
struct ColourStop { float t = 0.0f; glm::vec4 rgba{1.0f}; };
struct SizeStop   { float t = 0.0f; float scale = 1.0f; };

struct ParticleEmitterDesc {
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float angleDegrees = 20.0f;      // cone half-spread
    float emissionRate = 20.0f;      // particles/sec (looping)
    float ttlMin = 0.30f, ttlMax = 0.60f;
    float velocityMin = 0.30f, velocityMax = 0.70f;
    glm::vec4 startColour{1.0f};     // used when colourRamp is empty
};

// Ogre-agnostic description of one particle effect. eng::Particles translates
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
};

} // namespace eng
