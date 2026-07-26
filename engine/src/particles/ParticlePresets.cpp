#include <eng/particles/ParticlePresets.h>

#include <eng/Renderer.h>

namespace eng::particle_presets {
namespace {

ParticleEmitterDesc emitter(glm::vec3 direction, float angle, float rate,
                            float ttlMin, float ttlMax,
                            float velocityMin, float velocityMax)
{
    ParticleEmitterDesc e;
    e.direction = direction;
    e.angleDegrees = angle;
    e.emissionRate = rate;
    e.ttlMin = ttlMin;
    e.ttlMax = ttlMax;
    e.velocityMin = velocityMin;
    e.velocityMax = velocityMax;
    return e;
}

ParticleEffectDesc base(const char* name, const char* material,
                        float width, float height, int quota)
{
    ParticleEffectDesc d;
    d.name = name;
    d.material = material;
    d.baseWidth = width;
    d.baseHeight = height;
    d.quota = quota;
    return d;
}

void add(Renderer& renderer, ParticleEffectDesc effect)
{
    renderer.registerParticleEffect(effect);
}

} // namespace

void registerDefaults(Renderer& renderer)
{
    {
        auto d = base(Fire, "Engine/Particles/Fire", 0.16f, 0.19f, 32);
        d.acceleration = {0, 0.35f, 0};
        d.rotationJitterDeg = 70;
        d.scaleJitter = 0.16f;
        d.emitters = {emitter({0,1,0}, 18, 16, 0.28f, 0.52f, 0.18f, 0.45f)};
        d.colourRamp = {{0, {1,1,1,0.95f}}, {1, {1,0.35f,0.08f,0}}};
        d.sizeRamp = {{0, 1.05f}, {1, 0.28f}};
        add(renderer, std::move(d));
    }
    {
        auto d = base(Smoke, "Engine/Particles/Smoke", 0.24f, 0.24f, 40);
        d.acceleration = {0.03f, 0.12f, 0.01f};
        d.rotationJitterDeg = 30;
        d.scaleJitter = 0.25f;
        d.emitters = {emitter({0,1,0}, 24, 7, 0.9f, 1.8f, 0.08f, 0.25f)};
        d.colourRamp = {{0, {0.75f,0.72f,0.70f,0.42f}},
                        {1, {0.35f,0.37f,0.42f,0}}};
        d.sizeRamp = {{0, 0.55f}, {1, 2.2f}};
        add(renderer, std::move(d));
    }
    {
        auto d = base(Poison, "Engine/Particles/Poison", 0.18f, 0.22f, 48);
        d.acceleration = {0, 0.18f, 0};
        d.rotationJitterDeg = 80;
        d.scaleJitter = 0.20f;
        d.emitters = {emitter({0,1,0}, 42, 13, 0.55f, 1.15f, 0.10f, 0.38f)};
        d.colourRamp = {{0, {0.75f,1,0.55f,0.9f}},
                        {1, {0.35f,0.8f,0.15f,0}}};
        d.sizeRamp = {{0, 0.75f}, {1, 1.35f}};
        add(renderer, std::move(d));
    }
    {
        auto d = base(Rain, "Engine/Particles/Rain", 0.018f, 0.24f, 240);
        auto e = emitter({0,-1,0}, 1.5f, 220, 0.25f, 0.38f, 10, 14);
        e.shape = ParticleEmitterShape::Box;
        e.boxSize = {10, 0.15f, 10};
        d.emitters = {e};
        d.colourRamp = {{0, {0.65f,0.76f,0.92f,0.58f}},
                        {1, {0.48f,0.60f,0.78f,0.08f}}};
        d.qualityWeight = 1;
        add(renderer, std::move(d));
    }
    {
        auto d = base(LavaAsh, "Engine/Particles/LavaAsh", 0.10f, 0.10f, 40);
        d.acceleration = {0, -0.55f, 0};
        d.rotationJitterDeg = 150;
        d.scaleJitter = 0.30f;
        d.emitters = {emitter({0,1,0}, 32, 4, 0.65f, 1.3f, 0.55f, 1.45f)};
        d.colourRamp = {{0, {1,1,1,1}}, {1, {0.35f,0.08f,0.02f,0}}};
        d.sizeRamp = {{0, 0.8f}, {1, 0.25f}};
        add(renderer, std::move(d));
    }
    {
        auto d = base(HitSparks, "Engine/Particles/LavaAsh", 0.075f, 0.075f, 32);
        d.loop = false; d.burstCount = 18;
        d.acceleration = {0, -5, 0};
        d.rotationJitterDeg = 180; d.scaleJitter = 0.25f;
        d.emitters = {emitter({0,1,0}, 180, 1, 0.12f, 0.28f, 1.8f, 4.5f)};
        d.colourRamp = {{0, {1,0.9f,0.5f,1}}, {1, {1,0.15f,0.02f,0}}};
        d.sizeRamp = {{0, 1}, {1, 0.1f}};
        add(renderer, std::move(d));
    }
    {
        auto d = base(FootstepDust, "Engine/Particles/Smoke", 0.13f, 0.10f, 20);
        d.loop = false; d.burstCount = 7;
        d.acceleration = {0, -0.3f, 0};
        d.rotationJitterDeg = 90; d.scaleJitter = 0.25f;
        d.emitters = {emitter({0,1,0}, 70, 1, 0.22f, 0.42f, 0.15f, 0.55f)};
        d.colourRamp = {{0, {0.65f,0.58f,0.48f,0.35f}},
                        {1, {0.45f,0.40f,0.35f,0}}};
        d.sizeRamp = {{0, 0.5f}, {1, 1.5f}};
        add(renderer, std::move(d));
    }
    {
        auto d = base(PickupBurst, "Engine/Particles/Poison", 0.10f, 0.10f, 32);
        d.loop = false; d.burstCount = 16;
        d.acceleration = {0, -1.5f, 0};
        d.rotationJitterDeg = 180; d.scaleJitter = 0.2f;
        d.emitters = {emitter({0,1,0}, 180, 1, 0.25f, 0.55f, 0.6f, 1.8f)};
        d.colourRamp = {{0, {1,1,0.65f,1}}, {1, {0.25f,1,0.55f,0}}};
        d.sizeRamp = {{0, 1}, {1, 0.18f}};
        add(renderer, std::move(d));
    }
}

} // namespace eng::particle_presets
