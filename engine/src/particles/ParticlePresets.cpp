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
        auto d = base(Fire, "Engine/Particles/Fire", 0.32f, 0.38f, 96);
        d.acceleration = {0, 0.35f, 0};
        d.rotationJitterDeg = 70;
        d.scaleJitter = 0.16f;
        d.emitters = {emitter({0,1,0}, 24, 42, 0.38f, 0.78f, 0.16f, 0.56f)};
        d.colourRamp = {{0, {1.35f,0.12f,0.025f,0.98f}},
                        {0.42f, {1.65f,0.62f,0.04f,0.9f}},
                        {1, {0.72f,0.025f,0.005f,0}}};
        d.sizeRamp = {{0, 1.05f}, {1, 0.28f}};
        add(renderer, std::move(d));
    }
    {
        auto d = base(Smoke, "Engine/Particles/Smoke", 0.32f, 0.32f, 64);
        d.acceleration = {0.03f, 0.12f, 0.01f};
        d.rotationJitterDeg = 30;
        d.scaleJitter = 0.25f;
        d.emitters = {emitter({0,1,0}, 26, 11, 1.0f, 2.0f, 0.08f, 0.28f)};
        d.colourRamp = {{0, {0.75f,0.72f,0.70f,0.42f}},
                        {1, {0.35f,0.37f,0.42f,0}}};
        d.sizeRamp = {{0, 0.55f}, {1, 2.2f}};
        add(renderer, std::move(d));
    }
    {
        auto d = base(Poison, "Engine/Particles/Poison", 0.20f, 0.26f, 72);
        d.acceleration = {0, 0.18f, 0};
        d.rotationJitterDeg = 80;
        d.scaleJitter = 0.20f;
        d.emitters = {emitter({0,1,0}, 44, 22, 0.65f, 1.35f, 0.09f, 0.42f)};
        d.colourRamp = {{0, {0.18f,1.30f,0.045f,0.94f}},
                        {0.5f, {0.62f,1.48f,0.08f,0.78f}},
                        {1, {0.035f,0.48f,0.012f,0}}};
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
        auto d = base(LavaAsh, "Engine/Particles/LavaAsh", 0.14f, 0.14f, 64);
        d.acceleration = {0, -0.55f, 0};
        d.rotationJitterDeg = 150;
        d.scaleJitter = 0.30f;
        d.emitters = {emitter({0,1,0}, 36, 8, 0.72f, 1.55f, 0.48f, 1.55f)};
        d.colourRamp = {{0, {1.55f,0.46f,0.025f,1}},
                        {0.45f, {1.15f,0.10f,0.008f,0.9f}},
                        {1, {0.22f,0.012f,0.004f,0}}};
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
    {
        auto d = base(ArcaneMotes, "Engine/Particles/ArcaneMote",
                      0.13f, 0.13f, 72);
        d.acceleration = {0.08f, 0.10f, -0.06f};
        d.rotationJitterDeg = 180;
        d.scaleJitter = 0.32f;
        d.emitters = {emitter({0,1,0}, 78, 24, 0.7f, 1.5f, 0.12f, 0.62f)};
        d.colourRamp = {{0, {1.28f,0.12f,1.42f,1}},
                        {0.5f, {0.22f,0.55f,1.55f,0.88f}},
                        {1, {0.04f,0.08f,0.52f,0}}};
        d.sizeRamp = {{0, 0.35f}, {0.3f, 1.15f}, {1, 0.12f}};
        add(renderer, std::move(d));
    }
    {
        auto d = base(FrostShards, "Engine/Particles/FrostShard",
                      0.10f, 0.19f, 64);
        d.acceleration = {0, -1.4f, 0};
        d.rotationJitterDeg = 120;
        d.scaleJitter = 0.24f;
        d.emitters = {emitter({0,1,0}, 46, 16, 0.42f, 0.9f, 0.45f, 1.35f)};
        d.colourRamp = {{0, {0.20f,1.20f,1.55f,1}},
                        {0.55f, {0.10f,0.48f,1.32f,0.82f}},
                        {1, {0.02f,0.12f,0.48f,0}}};
        d.sizeRamp = {{0, 0.7f}, {0.55f, 1.15f}, {1, 0.2f}};
        add(renderer, std::move(d));
    }
    {
        auto d = base(ToxicBubbles, "Engine/Particles/ToxicBubble",
                      0.16f, 0.16f, 72);
        d.acceleration = {0, 0.32f, 0};
        d.rotationJitterDeg = 30;
        d.scaleJitter = 0.38f;
        d.emitters = {emitter({0,1,0}, 52, 20, 0.72f, 1.5f, 0.08f, 0.35f)};
        d.colourRamp = {{0, {0.12f,1.38f,0.025f,0.86f}},
                        {0.62f, {0.68f,1.52f,0.05f,0.64f}},
                        {1, {0.04f,0.42f,0.01f,0}}};
        d.sizeRamp = {{0, 0.42f}, {0.65f, 1.2f}, {1, 1.45f}};
        add(renderer, std::move(d));
    }
    {
        auto d = base(PortalWisps, "Engine/Particles/PortalWisp",
                      0.18f, 0.27f, 96);
        d.acceleration = {0, 0.14f, 0};
        d.rotationJitterDeg = 55;
        d.scaleJitter = 0.30f;
        auto e = emitter({0,1,0}, 68, 30, 0.8f, 1.8f, 0.08f, 0.48f);
        e.shape = ParticleEmitterShape::Box;
        e.boxSize = {3.45f, 2.55f, 0.16f};
        d.emitters = {e};
        d.colourRamp = {{0, {0.38f,1.45f,0.08f,0.92f}},
                        {0.5f, {0.10f,0.72f,1.35f,0.76f}},
                        {1, {0.08f,0.12f,0.58f,0}}};
        d.sizeRamp = {{0, 0.35f}, {0.42f, 1.18f}, {1, 0.18f}};
        add(renderer, std::move(d));
    }
}

} // namespace eng::particle_presets
