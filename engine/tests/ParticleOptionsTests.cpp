#include <eng/particles/ParticleEffectDesc.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "ParticleOptionsTests: " << message << '\n';
        std::exit(1);
    }
}

bool equal(glm::vec3 a, glm::vec3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool equal(glm::vec4 a, glm::vec4 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

} // namespace

int main()
{
    using namespace eng;

    {
        require(!particleSystemLifetimeExpired(false, 100.0f, 1.0f),
                "looping systems must not retire on a lifetime deadline");
        require(!particleSystemLifetimeExpired(true, 0.99f, 1.0f),
                "one-shot systems must remain live before their deadline");
        require(particleSystemLifetimeExpired(true, 1.0f, 1.0f),
                "one-shot systems must retire exactly at their deadline");
        require(particleSystemLifetimeExpired(true, 1.01f, 1.0f),
                "one-shot systems must retire after their deadline");
    }

    {
        const ParticleSpawnOptions options;
        require(options.sizeScale == 1.0f, "size scale default is not identity");
        require(options.amountScale == 1.0f, "amount scale default is not identity");
        require(options.lifetimeScale == 1.0f,
                "lifetime scale default is not identity");
        require(options.speedScale == 1.0f, "speed scale default is not identity");
        require(options.radiusScale == 1.0f, "radius scale default is not identity");
        require(options.emitterRadius == 0.0f,
                "point emitter radius default changes the preset");
        require(equal(options.colourTint, glm::vec4(1.0f)),
                "colour tint default is not identity");
        require(equal(options.localOffset, glm::vec3(0.0f)),
                "local offset default changes the preset");
    }

    {
        ParticleSpawnOptions invalid;
        invalid.sizeScale = -2.0f;
        invalid.amountScale = -3.0f;
        invalid.lifetimeScale = -4.0f;
        invalid.speedScale = -5.0f;
        invalid.radiusScale = -6.0f;
        invalid.emitterRadius = -7.0f;
        invalid.colourTint = {-0.1f, -1.0f, -2.0f, -3.0f};
        invalid.localOffset = {2.0f, -4.0f, 8.0f};

        const ParticleSpawnOptions clean = sanitizeParticleSpawnOptions(invalid);
        require(clean.sizeScale == 0.001f, "negative size scale was not clamped");
        require(clean.amountScale == 0.0f, "negative amount scale was not clamped");
        require(clean.lifetimeScale == 0.001f,
                "negative lifetime scale was not clamped");
        require(clean.speedScale == 0.0f, "negative speed scale was not clamped");
        require(clean.radiusScale == 0.001f, "negative radius scale was not clamped");
        require(clean.emitterRadius == 0.0f,
                "negative emitter radius was not clamped");
        require(equal(clean.colourTint, glm::vec4(0.0f)),
                "negative tint components were not clamped");
        require(equal(clean.localOffset, invalid.localOffset),
                "finite local offset was changed");
    }

    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        ParticleSpawnOptions invalid;
        invalid.sizeScale = nan;
        invalid.amountScale = infinity;
        invalid.lifetimeScale = -infinity;
        invalid.speedScale = nan;
        invalid.radiusScale = infinity;
        invalid.emitterRadius = nan;
        invalid.colourTint = {nan, infinity, -infinity, nan};
        invalid.localOffset = {nan, infinity, -infinity};

        const ParticleSpawnOptions clean = sanitizeParticleSpawnOptions(invalid);
        require(clean.sizeScale == 1.0f, "non-finite size scale did not use identity");
        require(clean.amountScale == 1.0f,
                "non-finite amount scale did not use identity");
        require(clean.lifetimeScale == 1.0f,
                "non-finite lifetime scale did not use identity");
        require(clean.speedScale == 1.0f,
                "non-finite speed scale did not use identity");
        require(clean.radiusScale == 1.0f,
                "non-finite radius scale did not use identity");
        require(clean.emitterRadius == 0.0f,
                "non-finite emitter radius did not use point identity");
        require(equal(clean.colourTint, glm::vec4(1.0f)),
                "non-finite tint did not use identity");
        require(equal(clean.localOffset, glm::vec3(0.0f)),
                "non-finite local offset did not use zero");
    }

    {
        ParticleEffectDesc preset;
        preset.baseWidth = 2.0f;
        preset.baseHeight = 3.0f;
        ParticleEmitterDesc point;
        point.position = {1.0f, 2.0f, 3.0f};
        point.emissionRate = 10.0f;
        point.ttlMin = 1.0f;
        point.ttlMax = 2.0f;
        point.velocityMin = 3.0f;
        point.velocityMax = 4.0f;
        point.startColour = {0.5f, 0.25f, 1.0f, 0.8f};
        ParticleEmitterDesc box = point;
        box.shape = ParticleEmitterShape::Box;
        box.boxSize = {8.0f, 6.0f, 4.0f};
        box.ttlMax = 4.0f;
        preset.emitters = {point, box};

        ParticleSpawnOptions options;
        options.sizeScale = 2.0f;
        options.amountScale = 0.5f;
        options.lifetimeScale = 3.0f;
        options.speedScale = 4.0f;
        options.radiusScale = 0.5f;
        options.emitterRadius = 2.0f;
        options.colourTint = {0.5f, 2.0f, 0.25f, 0.5f};
        options.localOffset = {0.5f, 1.0f, 1.5f};

        const ResolvedParticleSpawn custom =
            resolveParticleSpawn(preset, options, {2.0f, 4.0f, 6.0f}, 0.5f);
        require(custom.defaultWidth == 4.0f &&
                    custom.defaultHeight == 6.0f,
                "custom dimensions were not resolved");
        require(custom.emitters.size() == 2,
                "resolved emitter count changed");
        require(custom.emitters[0].shape == ParticleEmitterShape::Box &&
                    equal(custom.emitters[0].boxSize, glm::vec3(4.0f)),
                "point radius did not resolve to a box volume");
        require(equal(custom.emitters[1].boxSize,
                      glm::vec3(4.0f, 3.0f, 2.0f)),
                "box radius scale was not resolved");
        require(equal(custom.emitters[0].position,
                      glm::vec3(3.5f, 7.0f, 10.5f)),
                "emitter position did not include both offsets");
        require(custom.emitters[0].emissionRate == 2.5f,
                "continuous amount and quality scales were not resolved");
        require(custom.emitters[0].ttlMin == 3.0f &&
                    custom.emitters[0].ttlMax == 6.0f,
                "lifetime range was not resolved");
        require(custom.emitters[0].velocityMin == 12.0f &&
                    custom.emitters[0].velocityMax == 16.0f,
                "velocity range was not resolved");
        require(equal(custom.emitters[0].colour,
                      glm::vec4(0.25f, 0.5f, 0.25f, 0.4f)),
                "emitter tint was not resolved");
        require(custom.maxTtl == 12.0f,
                "custom maximum TTL was not resolved");

        // Resolve identity after custom. The second result must derive only
        // from the immutable preset, never from the previous checkout.
        const ResolvedParticleSpawn identity =
            resolveParticleSpawn(preset, ParticleSpawnOptions{});
        require(identity.defaultWidth == 2.0f &&
                    identity.defaultHeight == 3.0f,
                "custom dimensions leaked into identity resolution");
        require(identity.emitters[0].shape == ParticleEmitterShape::Point,
                "custom point volume leaked into identity resolution");
        require(equal(identity.emitters[1].boxSize,
                      glm::vec3(8.0f, 6.0f, 4.0f)),
                "custom box volume leaked into identity resolution");
        require(equal(identity.emitters[0].position,
                      glm::vec3(1.0f, 2.0f, 3.0f)),
                "custom offset leaked into identity resolution");
        require(identity.emitters[0].emissionRate == 10.0f,
                "custom amount leaked into identity resolution");
        require(identity.emitters[0].ttlMin == 1.0f &&
                    identity.emitters[0].ttlMax == 2.0f,
                "custom lifetime leaked into identity resolution");
        require(identity.emitters[0].velocityMin == 3.0f &&
                    identity.emitters[0].velocityMax == 4.0f,
                "custom speed leaked into identity resolution");
        require(equal(identity.emitters[0].colour, point.startColour),
                "custom tint leaked into identity resolution");
        require(identity.maxTtl == 4.0f,
                "custom maximum TTL leaked into identity resolution");
    }

    {
        ParticleEffectDesc oneShot;
        oneShot.loop = false;
        oneShot.burstCount = 40.0f;
        ParticleEmitterDesc shortLived;
        shortLived.ttlMax = 2.0f;
        ParticleEmitterDesc longLived = shortLived;
        longLived.ttlMax = 4.0f;
        oneShot.emitters = {shortLived, longLived};

        ParticleSpawnOptions options;
        options.amountScale = 1.5f;
        options.lifetimeScale = 2.5f;
        const ResolvedParticleSpawn resolved =
            resolveParticleSpawn(oneShot, options, {}, 0.5f);
        require(resolved.emitters[0].emissionRate == 300.0f &&
                    resolved.emitters[0].emissionDuration == 0.05f,
                "one-shot burst count was not resolved per emitter");
        require(resolved.maxTtl == 10.0f,
                "one-shot retirement did not use overridden maximum TTL");
    }

    std::cout << "ParticleOptionsTests OK\n";
}
