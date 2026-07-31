// Headless tests for the particle simulation. ParticleSim and ParticleEmitters
// include neither Ogre nor Jolt, so everything here runs on a plain pool and a
// stub IParticleCollider; nothing reads the wall clock and every random draw is
// seeded, so a failure here is a real behaviour change and never a flake.
#include <particles/ParticleSim.h>

#include <eng/particles/ParticleCollider.h>
#include <eng/particles/ParticleEffectDesc.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <algorithm>
#include <set>
#include <tuple>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "ParticleSimTests: " << message << '\n';
        std::exit(1);
    }
}

std::string num(float v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", double(v));
    return std::string(buf);
}

std::string num(long long v) { return std::to_string(v); }

std::string vec(glm::vec3 v)
{
    return "(" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + ")";
}

std::string vec(glm::vec4 v)
{
    return "(" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + ", " +
           num(v.w) + ")";
}

bool near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

bool finite(glm::vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// A cone-spread looping emitter: the shared baseline for the determinism and
// steady-state cases.
eng::ParticleEffectDesc loopingEffect(float rate, float ttl)
{
    eng::ParticleEffectDesc desc;
    desc.name = "test_loop";
    desc.loop = true;
    desc.baseWidth = 0.1f;
    desc.baseHeight = 0.1f;
    eng::ParticleEmitterDesc emitter;
    emitter.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    emitter.angleDegrees = 35.0f;
    emitter.emissionRate = rate;
    emitter.ttlMin = ttl;
    emitter.ttlMax = ttl;
    emitter.velocityMin = 1.0f;
    emitter.velocityMax = 4.0f;
    desc.emitters.push_back(emitter);
    return desc;
}

// Counts every trace and can be told to report a fixed hit, which is how the
// three collide responses are driven without a world.
struct StubCollider final : eng::IParticleCollider {
    bool hit = false;
    glm::vec3 hitPoint{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    int calls = 0;
    int callsThisFrame = 0;
    std::vector<glm::vec3> origins;
    bool record = false;

    void beginFrame()
    {
        callsThisFrame = 0;
        if (record)
            origins.clear();
    }

    bool sweep(glm::vec3 from, glm::vec3 /*to*/, glm::vec3& outPos,
               glm::vec3& outNormal) override
    {
        ++calls;
        ++callsThisFrame;
        if (record)
            origins.push_back(from);
        if (!hit)
            return false;
        outPos = hitPoint;
        outNormal = normal;
        return true;
    }
};

// --- 1. determinism --------------------------------------------------------

void testDeterminism()
{
    using namespace eng;
    const ParticleEffectDesc desc = loopingEffect(90.0f, 0.8f);
    const ResolvedParticleSpawn resolved =
        resolveParticleSpawn(desc, ParticleSpawnOptions{});

    const auto run = [&](ParticleSim& sim) {
        sim.reserve(2048);
        const uint16_t fx = sim.registerEffect(desc);
        const uint32_t inst =
            sim.addInstance(fx, resolved, glm::mat4(1.0f), 0xC0FFEEu);
        require(inst != ParticleSim::kInvalidInstance,
                "addInstance rejected a registered effect");
        std::vector<DecalRequest> decals;
        for (int i = 0; i < 120; ++i)
            sim.update(1.0f / 60.0f, decals);
    };

    ParticleSim a, b;
    run(a);
    run(b);

    require(a.liveCount() == b.liveCount(),
            "same-seed sims disagreed on live count: a=" +
                num((long long)a.liveCount()) + " b=" +
                num((long long)b.liveCount()));
    require(a.liveCount() > 0,
            "determinism run produced no particles at all (expected > 0)");

    const ParticlePool& pa = a.pool();
    const ParticlePool& pb = b.pool();
    for (uint32_t i = 0; i < a.liveCount(); ++i) {
        require(std::memcmp(&pa.pos[i], &pb.pos[i], sizeof(glm::vec3)) == 0,
                "same-seed sims diverged at particle " + num((long long)i) +
                    ": a=" + vec(pa.pos[i]) + " b=" + vec(pb.pos[i]));
        require(std::memcmp(&pa.vel[i], &pb.vel[i], sizeof(glm::vec3)) == 0,
                "same-seed sims diverged in velocity at particle " +
                    num((long long)i) + ": a=" + vec(pa.vel[i]) + " b=" +
                    vec(pb.vel[i]));
        require(pa.age[i] == pb.age[i] && pa.life[i] == pb.life[i],
                "same-seed sims diverged in age/life at particle " +
                    num((long long)i));
    }

    // A different seed must actually change the draw, otherwise the identity
    // above would be vacuous.
    ParticleSim c;
    c.reserve(2048);
    const uint16_t fx = c.registerEffect(desc);
    c.addInstance(fx, resolved, glm::mat4(1.0f), 0xDEADBEEFu);
    std::vector<DecalRequest> decals;
    for (int i = 0; i < 120; ++i)
        c.update(1.0f / 60.0f, decals);
    bool differs = false;
    const uint32_t n = std::min(a.liveCount(), c.liveCount());
    for (uint32_t i = 0; i < n && !differs; ++i)
        differs = std::memcmp(&pa.pos[i], &c.pool().pos[i],
                              sizeof(glm::vec3)) != 0;
    require(differs || a.liveCount() != c.liveCount(),
            "a different instance seed produced an identical pool, so the "
            "seed is not reaching the emitters");
}

// --- 2. steady-state count -------------------------------------------------

void testSteadyState()
{
    using namespace eng;
    const float rate = 120.0f;
    const float ttl = 1.0f;
    const ParticleEffectDesc desc = loopingEffect(rate, ttl);

    ParticleSim sim;
    sim.reserve(4096);
    const uint16_t fx = sim.registerEffect(desc);
    const uint32_t inst =
        sim.addInstance(fx, resolveParticleSpawn(desc, ParticleSpawnOptions{}),
                        glm::mat4(1.0f), 7u);
    std::vector<DecalRequest> decals;
    for (int i = 0; i < 600; ++i)   // 10 s at 60 Hz: well past R*L settling
        sim.update(1.0f / 60.0f, decals);

    const float expected = rate * ttl;
    const float actual = float(sim.liveCount());
    require(std::fabs(actual - expected) <= 4.0f,
            "looping emitter did not settle at rate*lifetime: expected ~" +
                num(expected) + " (+/-4), got " + num(actual));
    require(sim.liveCount(fx) == sim.liveCount(),
            "per-effect index count disagreed with the pool: effect=" +
                num((long long)sim.liveCount(fx)) + " pool=" +
                num((long long)sim.liveCount()));
    require(sim.instanceActive(inst),
            "a looping instance retired on its own without stopInstance");

    // Stopping drains rather than killing: the count must fall to zero within
    // one particle lifetime and the slot must then release.
    sim.stopInstance(inst);
    for (int i = 0; i < 70; ++i)
        sim.update(1.0f / 60.0f, decals);
    require(sim.liveCount() == 0,
            "a stopped emitter still had " + num((long long)sim.liveCount()) +
                " particles one lifetime later (expected 0)");
    require(!sim.instanceActive(inst),
            "a drained instance slot was never released");
}

// --- 3. one-shot retirement ------------------------------------------------

void testOneShotRetirement()
{
    using namespace eng;
    ParticleEffectDesc desc;
    desc.name = "test_burst";
    desc.loop = false;
    desc.burstCount = 24.0f;
    ParticleEmitterDesc emitter;
    emitter.angleDegrees = 20.0f;
    emitter.ttlMin = 0.30f;
    emitter.ttlMax = 0.30f;
    emitter.velocityMin = 1.0f;
    emitter.velocityMax = 2.0f;
    desc.emitters.push_back(emitter);

    const ResolvedParticleSpawn resolved =
        resolveParticleSpawn(desc, ParticleSpawnOptions{});
    // resolveParticleSpawn turns a burst into a 0.05 s emission window; the
    // instance must outlive window + maxTtl and not one frame less.
    const float window = 0.05f;
    const float maxLife = window + resolved.maxTtl;
    require(near(resolved.maxTtl, 0.30f),
            "burst maxTtl was not resolved: expected 0.3, got " +
                num(resolved.maxTtl));

    ParticleSim sim;
    sim.reserve(512);
    const uint16_t fx = sim.registerEffect(desc);
    const uint32_t inst =
        sim.addInstance(fx, resolved, glm::mat4(1.0f), 99u);
    std::vector<DecalRequest> decals;

    const float dt = 0.01f;
    float t = 0.0f;
    for (int i = 0; i < 30; ++i) {   // up to t = 0.30, still inside maxLife
        sim.update(dt, decals);
        t += dt;
        require(sim.instanceActive(inst),
                "one-shot instance retired early at t=" + num(t) +
                    " (emission window + maxTtl = " + num(maxLife) + ")");
        require(sim.liveCount() > 0,
                "one-shot burst had no live particles at t=" + num(t));
    }
    // The `emitting` flag is only cleared at maxLife -- the emitter itself
    // stops earlier, on the resolved duration -- so what is asserted here is
    // that no *new* particles appear once the window has closed.
    const uint32_t afterWindow = sim.liveCount();
    sim.update(dt, decals);
    t += dt;
    require(sim.liveCount() <= afterWindow,
            "the burst emitted again after its " + num(window) +
                "s window: " + num((long long)afterWindow) + " -> " +
                num((long long)sim.liveCount()));
    // A burst emits over the resolved 0.05 s window, so the exact total is a
    // function of the step: the frame that lands on age >= duration is skipped
    // and the fractional accumulator is dropped. It must never exceed
    // burst_count, and must not lose most of it either.
    const uint32_t emitted = afterWindow;
    require(emitted <= 24,
            "burst emitted more than burst_count: " +
                num((long long)emitted) + " > 24");
    require(emitted >= 16,
            "burst lost most of burst_count over its emission window: " +
                num((long long)emitted) + " of 24, dt=" + num(dt));

    for (int i = 0; i < 20; ++i)     // to t = 0.50, past window + maxTtl
        sim.update(dt, decals);
    require(sim.liveCount() == 0,
            "one-shot particles outlived their ttl: " +
                num((long long)sim.liveCount()) + " still alive at t=0.5");
    require(!sim.instanceActive(inst),
            "one-shot instance never retired: still active at t=0.5 with "
            "maxLife=" + num(maxLife));
}

// --- 4. ramps --------------------------------------------------------------

void testRamps()
{
    using namespace eng;

    require(evalSizeRamp({}, 0.5f) == 1.0f,
            "an empty size ramp must be identity, got " +
                num(evalSizeRamp({}, 0.5f)));
    const glm::vec4 fallback(0.25f, 0.5f, 0.75f, 1.0f);
    require(evalColourRamp({}, 0.5f, fallback) == fallback,
            "an empty colour ramp must return the fallback, got " +
                vec(evalColourRamp({}, 0.5f, fallback)));

    const std::vector<SizeStop> single{{0.4f, 3.0f}};
    for (float t : {0.0f, 0.4f, 1.0f}) {
        const float got = evalSizeRamp(single, t);
        require(got == 3.0f,
                "a single-stop size ramp must be constant: t=" + num(t) +
                    " expected 3, got " + num(got));
    }
    const std::vector<ColourStop> singleColour{{0.6f, glm::vec4(0.1f, 0.2f,
                                                               0.3f, 0.4f)}};
    require(evalColourRamp(singleColour, 0.0f, fallback) ==
                singleColour[0].rgba,
            "a single-stop colour ramp must be constant before its stop");
    require(evalColourRamp(singleColour, 1.0f, fallback) ==
                singleColour[0].rgba,
            "a single-stop colour ramp must be constant after its stop");

    const std::vector<SizeStop> ramp{{0.0f, 1.0f}, {0.5f, 2.0f}, {1.0f, 0.0f}};
    struct { float t, expect; } sizeCases[] = {
        {-1.0f, 1.0f},   // clamped below the first stop
        {0.0f, 1.0f}, {0.25f, 1.5f}, {0.5f, 2.0f},
        {0.75f, 1.0f}, {1.0f, 0.0f},
        {2.0f, 0.0f},    // clamped above the last stop
    };
    for (const auto& c : sizeCases) {
        const float got = evalSizeRamp(ramp, c.t);
        require(near(got, c.expect),
                "size ramp at t=" + num(c.t) + ": expected " +
                    num(c.expect) + ", got " + num(got));
    }
    require(near(evalSizeRamp(ramp, std::numeric_limits<float>::quiet_NaN()),
                 1.0f),
            "a NaN t must clamp to the ramp start, got " +
                num(evalSizeRamp(ramp,
                                 std::numeric_limits<float>::quiet_NaN())));

    const std::vector<ColourStop> colours{
        {0.0f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)},
        {1.0f, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)}};
    const glm::vec4 mid = evalColourRamp(colours, 0.5f, fallback);
    require(near(mid.r, 0.5f) && near(mid.g, 0.0f) && near(mid.b, 0.5f) &&
                near(mid.a, 0.5f),
            "colour ramp midpoint: expected (0.5, 0, 0.5, 0.5), got " +
                vec(mid));

    // Coincident stops are how a hard cut is authored: the later stop wins.
    const std::vector<ColourStop> cut{
        {0.0f, glm::vec4(1.0f)}, {0.5f, glm::vec4(1.0f)},
        {0.5f, glm::vec4(0.0f)}, {1.0f, glm::vec4(0.0f)}};
    const glm::vec4 after = evalColourRamp(cut, 0.5f, fallback);
    require(after == glm::vec4(1.0f),
            "a coincident-stop cut resolved wrong at t=0.5: got " +
                vec(after));
    require(evalColourRamp(cut, 0.6f, fallback) == glm::vec4(0.0f),
            "a coincident-stop cut did not take effect past the cut, got " +
                vec(evalColourRamp(cut, 0.6f, fallback)));
}

// --- 5. collision budget ---------------------------------------------------

void testCollisionBudget()
{
    using namespace eng;
    // Stationary particles: with zero velocity and no acceleration each one
    // keeps a fixed, distinct position, which is what lets the stub identify
    // who was traced without the sim exposing particle ids.
    ParticleEffectDesc desc;
    desc.name = "test_budget";
    desc.loop = false;
    desc.burstCount = 100.0f;
    desc.collideResponse = ParticleCollideResponse::Die;
    ParticleEmitterDesc emitter;
    emitter.shape = ParticleEmitterShape::Box;
    emitter.boxSize = glm::vec3(20.0f);
    emitter.angleDegrees = 0.0f;
    emitter.velocityMin = 0.0f;
    emitter.velocityMax = 0.0f;
    emitter.ttlMin = 100.0f;
    emitter.ttlMax = 100.0f;
    desc.emitters.push_back(emitter);

    ParticleSim sim;
    sim.reserve(512);
    StubCollider collider;
    collider.hit = false;   // no hit: nobody dies, the candidate set is stable
    collider.record = true;
    sim.setCollider(&collider);
    const uint32_t budget = 7;
    sim.setRayBudget(budget);
    const uint16_t fx = sim.registerEffect(desc);
    sim.addInstance(fx, resolveParticleSpawn(desc, ParticleSpawnOptions{}),
                    glm::mat4(1.0f), 1234u);

    std::vector<DecalRequest> decals;
    // First update emits; tracing starts on the update after the spawn.
    for (int i = 0; i < 10; ++i)
        sim.update(0.01f, decals);
    // The exact burst total depends on the step (see testOneShotRetirement),
    // so the fixture measures what it actually got and derives the number of
    // frames a complete round-robin sweep should take from that.
    const uint32_t count = sim.liveCount();
    require(count > 4 * budget,
            "budget fixture needs demand well past the budget: got " +
                num((long long)count) + " particles for a budget of " +
                num((long long)budget));

    const auto key = [](glm::vec3 p) {
        return std::make_tuple(p.x, p.y, p.z);
    };
    std::set<std::tuple<float, float, float>> all;
    for (uint32_t i = 0; i < count; ++i)
        all.insert(key(sim.pool().pos[i]));
    require(all.size() == count,
            "fixture positions are not distinct (" +
                num((long long)all.size()) + " unique of " +
                num((long long)count) + "), so coverage cannot be measured");

    std::set<std::tuple<float, float, float>> seen;
    const int frames = int((count + budget - 1) / budget);
    for (int f = 0; f < frames; ++f) {
        collider.beginFrame();
        sim.update(0.01f, decals);
        require(collider.callsThisFrame == int(budget),
                "ray budget violated on frame " + num((long long)f) +
                    ": expected exactly " + num((long long)budget) +
                    " sweeps, saw " + num((long long)collider.callsThisFrame));
        require(sim.raysLastFrame() == budget,
                "raysLastFrame reported " +
                    num((long long)sim.raysLastFrame()) + ", expected " +
                    num((long long)budget));
        for (glm::vec3 p : collider.origins)
            seen.insert(key(p));
    }
    require(sim.liveCount() == count,
            "a non-hitting collider still retired particles: " +
                num((long long)sim.liveCount()) + " of " +
                num((long long)count) + " left");
    require(seen.size() == count,
            "round-robin starved the tail: only " +
                num((long long)seen.size()) + " of " +
                num((long long)count) +
                " particles were traced in " + num((long long)frames) +
                " frames at a budget of " + num((long long)budget));

    // With no collider installed nothing is traced at all.
    sim.setCollider(nullptr);
    const int before = collider.calls;
    sim.update(0.01f, decals);
    require(collider.calls == before,
            "sweeps were issued after the collider was detached");
    require(sim.raysLastFrame() == 0,
            "raysLastFrame was " + num((long long)sim.raysLastFrame()) +
                " with no collider installed, expected 0");
}

// --- 6. collide responses --------------------------------------------------

namespace response {

// One particle, fired straight down at a known speed, so a hit against an
// upward normal has an unambiguous expected reflection. A looping emitter at
// 10/s stepped once by 0.1 s produces exactly one particle; a one-shot burst
// could not, because its 0.05 s window closes before a rate of 20/s owes a
// whole particle.
eng::ParticleEffectDesc fallingEffect(eng::ParticleCollideResponse response)
{
    eng::ParticleEffectDesc desc;
    desc.name = "test_response";
    desc.loop = true;
    desc.collideResponse = response;
    desc.restitution = 0.5f;
    desc.friction = 0.25f;
    eng::ParticleEmitterDesc emitter;
    emitter.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    emitter.angleDegrees = 0.0f;      // no cone: the direction is exact
    emitter.emissionRate = 10.0f;
    emitter.velocityMin = 10.0f;
    emitter.velocityMax = 10.0f;
    emitter.ttlMin = 5.0f;
    emitter.ttlMax = 5.0f;
    desc.emitters.push_back(emitter);
    return desc;
}

uint16_t spawnOne(eng::ParticleSim& sim, const eng::ParticleEffectDesc& desc,
                  StubCollider& collider,
                  std::vector<eng::DecalRequest>& decals)
{
    using namespace eng;
    sim.reserve(64);
    sim.setCollider(&collider);
    const uint16_t fx = sim.registerEffect(desc);
    const uint32_t inst =
        sim.addInstance(fx, resolveParticleSpawn(desc, ParticleSpawnOptions{}),
                        glm::mat4(1.0f), 4242u);
    collider.hit = false;
    sim.update(0.1f, decals);   // emit exactly one, no hit reported yet
    sim.stopInstance(inst);     // no further emission; the particle ages on
    require(sim.liveCount() == 1,
            "response fixture expected exactly 1 particle, got " +
                num((long long)sim.liveCount()));
    collider.hit = true;
    return fx;
}

} // namespace response

void testCollideResponses()
{
    using namespace eng;

    {   // Die: retires on contact.
        ParticleSim sim;
        StubCollider collider;
        std::vector<DecalRequest> decals;
        response::spawnOne(sim,
                           response::fallingEffect(ParticleCollideResponse::Die),
                           collider, decals);
        sim.update(0.01f, decals);
        require(sim.liveCount() == 0,
                "collide=die did not retire the particle: " +
                    num((long long)sim.liveCount()) + " still alive");
        require(decals.empty(),
                "collide=die pushed " + num((long long)decals.size()) +
                    " decal requests, expected 0");
    }

    {   // Bounce: the normal component flips and the speed drops.
        ParticleSim sim;
        StubCollider collider;
        std::vector<DecalRequest> decals;
        const ParticleEffectDesc desc =
            response::fallingEffect(ParticleCollideResponse::Bounce);
        response::spawnOne(sim, desc, collider, decals);
        collider.hitPoint = glm::vec3(0.0f, -1.0f, 0.0f);
        collider.normal = glm::vec3(0.0f, 1.0f, 0.0f);

        const glm::vec3 before = sim.pool().vel[0];
        require(before.y < 0.0f,
                "bounce fixture particle was not falling: v=" + vec(before));
        sim.update(0.01f, decals);
        require(sim.liveCount() == 1,
                "collide=bounce retired the particle instead of reflecting");
        const glm::vec3 after = sim.pool().vel[0];
        require(after.y > 0.0f,
                "bounce did not flip the normal component: before " +
                    vec(before) + " after " + vec(after));
        require(near(after.y, -before.y * desc.restitution, 1e-3f),
                "bounce did not apply restitution " + num(desc.restitution) +
                    ": expected v.y=" + num(-before.y * desc.restitution) +
                    ", got " + num(after.y));
        require(glm::length(after) < glm::length(before),
                "bounce did not lose speed: |before|=" +
                    num(glm::length(before)) + " |after|=" +
                    num(glm::length(after)));
        require(sim.pool().pos[0].y >= collider.hitPoint.y,
                "bounce left the particle behind the surface: y=" +
                    num(sim.pool().pos[0].y) + ", hit at y=" +
                    num(collider.hitPoint.y));
        require(decals.empty(),
                "collide=bounce pushed decal requests, expected 0");
    }

    {   // Bounce with a tangential component: friction eats the tangent.
        ParticleSim sim;
        StubCollider collider;
        std::vector<DecalRequest> decals;
        ParticleEffectDesc desc =
            response::fallingEffect(ParticleCollideResponse::Bounce);
        desc.emitters[0].direction = glm::normalize(glm::vec3(1.0f, -1.0f, 0.0f));
        response::spawnOne(sim, desc, collider, decals);
        collider.hitPoint = glm::vec3(0.0f, -1.0f, 0.0f);
        collider.normal = glm::vec3(0.0f, 1.0f, 0.0f);

        const glm::vec3 before = sim.pool().vel[0];
        sim.update(0.01f, decals);
        const glm::vec3 after = sim.pool().vel[0];
        require(near(after.x, before.x * (1.0f - desc.friction), 1e-3f),
                "bounce did not apply friction " + num(desc.friction) +
                    " to the tangent: expected v.x=" +
                    num(before.x * (1.0f - desc.friction)) + ", got " +
                    num(after.x));
    }

    {   // Decal: retires and pushes exactly one request with the profile id.
        ParticleSim sim;
        StubCollider collider;
        std::vector<DecalRequest> decals;
        ParticleEffectDesc desc =
            response::fallingEffect(ParticleCollideResponse::Decal);
        desc.decalProfile = "blood_splat";
        response::spawnOne(sim, desc, collider, decals);
        collider.hitPoint = glm::vec3(2.0f, -1.0f, 3.0f);
        collider.normal = glm::vec3(0.0f, 2.0f, 0.0f);   // unnormalised on purpose

        sim.update(0.01f, decals);
        require(sim.liveCount() == 0,
                "collide=decal did not retire the particle: " +
                    num((long long)sim.liveCount()) + " still alive");
        require(decals.size() == 1,
                "collide=decal pushed " + num((long long)decals.size()) +
                    " requests, expected exactly 1");
        require(decals[0].profile == "blood_splat",
                "decal request carried profile '" + decals[0].profile +
                    "', expected 'blood_splat'");
        require(decals[0].position == collider.hitPoint,
                "decal request was placed at " + vec(decals[0].position) +
                    ", expected the hit point " + vec(collider.hitPoint));
        require(near(glm::length(decals[0].normal), 1.0f),
                "decal request normal was not normalised: " +
                    vec(decals[0].normal));
    }

    {   // Decal with no profile: dies silently rather than marking nothing.
        ParticleSim sim;
        StubCollider collider;
        std::vector<DecalRequest> decals;
        const ParticleEffectDesc desc =
            response::fallingEffect(ParticleCollideResponse::Decal);
        response::spawnOne(sim, desc, collider, decals);
        sim.update(0.01f, decals);
        require(sim.liveCount() == 0,
                "a profile-less decal particle survived its hit");
        require(decals.empty(),
                "a profile-less decal effect still queued " +
                    num((long long)decals.size()) + " requests, expected 0");
    }

    {   // None: never traced at all, so the collider is never called.
        ParticleSim sim;
        StubCollider collider;
        std::vector<DecalRequest> decals;
        const ParticleEffectDesc desc =
            response::fallingEffect(ParticleCollideResponse::None);
        response::spawnOne(sim, desc, collider, decals);
        for (int i = 0; i < 5; ++i)
            sim.update(0.01f, decals);
        require(collider.calls == 0,
                "collide=none still issued " + num((long long)collider.calls) +
                    " sweeps, expected 0");
        require(sim.liveCount() == 1,
                "collide=none particle was retired by a collider it never "
                "consulted");
    }
}

// --- 7. defensive input ----------------------------------------------------

void testDefensiveInput()
{
    using namespace eng;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    ParticleEffectDesc desc;
    desc.name = "test_hostile";
    desc.loop = true;
    desc.acceleration = glm::vec3(0.0f, -9.8f, 0.0f);
    desc.drag = 0.5f;
    ParticleEmitterDesc emitter;
    emitter.direction = glm::vec3(0.0f);      // zero length: no cone axis
    emitter.angleDegrees = 45.0f;
    emitter.emissionRate = 50.0f;
    emitter.ttlMin = -2.0f;                   // negative lifetime
    emitter.ttlMax = -1.0f;
    emitter.velocityMin = 1.0f;
    emitter.velocityMax = 3.0f;
    ParticleEmitterDesc hostile = emitter;
    hostile.ttlMin = 0.5f;
    hostile.ttlMax = 0.5f;
    hostile.direction = glm::vec3(nan, inf, -inf);
    desc.emitters.push_back(emitter);
    desc.emitters.push_back(hostile);

    ParticleSim sim;
    sim.reserve(1024);
    const uint16_t fx = sim.registerEffect(desc);
    const uint32_t inst =
        sim.addInstance(fx, resolveParticleSpawn(desc, ParticleSpawnOptions{}),
                        glm::mat4(1.0f), 31337u);
    require(inst != ParticleSim::kInvalidInstance,
            "a hostile-but-registered effect was refused an instance");

    std::vector<DecalRequest> decals;
    const float steps[] = {nan, inf, -inf, -1.0f, 0.0f, 1e9f, 1.0f / 60.0f};
    for (int repeat = 0; repeat < 20; ++repeat) {
        for (float dt : steps) {
            sim.update(dt, decals);
            for (uint32_t i = 0; i < sim.liveCount(); ++i) {
                require(finite(sim.pool().pos[i]),
                        "non-finite position " + vec(sim.pool().pos[i]) +
                            " after update(dt=" + num(dt) + ")");
                require(finite(sim.pool().vel[i]),
                        "non-finite velocity " + vec(sim.pool().vel[i]) +
                            " after update(dt=" + num(dt) + ")");
                require(std::isfinite(sim.pool().size[i]) &&
                            sim.pool().size[i] >= 0.0f,
                        "bad particle size " + num(sim.pool().size[i]) +
                            " after update(dt=" + num(dt) + ")");
                require(sim.pool().life[i] > 0.0f,
                        "a negative authored lifetime produced life=" +
                            num(sim.pool().life[i]) +
                            "; it must clamp above zero");
            }
        }
    }
    require(sim.liveCount() > 0,
            "the hostile fixture emitted nothing, so nothing was checked");

    // An unregistered effect id must be refused rather than indexed.
    require(sim.addInstance(uint16_t(500), ResolvedParticleSpawn{},
                            glm::mat4(1.0f), 1u) ==
                ParticleSim::kInvalidInstance,
            "addInstance accepted an unregistered effect id");
    require(!sim.instanceActive(ParticleSim::kInvalidInstance),
            "kInvalidInstance reported itself active");

    // killInstance drops the particles immediately, unlike stopInstance.
    sim.killInstance(inst);
    require(sim.liveCount() == 0,
            "killInstance left " + num((long long)sim.liveCount()) +
                " particles behind, expected 0");
    require(!sim.instanceActive(inst),
            "killInstance did not release the instance slot");
}

} // namespace

int main()
{
    testDeterminism();
    testSteadyState();
    testOneShotRetirement();
    testRamps();
    testCollisionBudget();
    testCollideResponses();
    testDefensiveInput();
    std::cout << "ParticleSimTests OK\n";
}
