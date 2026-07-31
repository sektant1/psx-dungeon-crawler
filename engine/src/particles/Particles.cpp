#include "Particles.h"

#include "ParticleEmitters.h"

#include <eng/Log.h>
#include <eng/render/PrototypeAssets.h>

#include <OgreCamera.h>
#include <OgreMaterialManager.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace eng {
namespace {

// Sized so a scene can run every authored effect at once without the pool
// refusing spawns. Particles are 80 bytes across the SoA arrays, so this is a
// few hundred kilobytes reserved once at startup and never grown.
constexpr size_t kPoolCapacity = 8192;

bool validSpawnOptions(const ParticleSpawnOptions& o)
{
    const auto finiteAtLeast = [](float value, float minimum) {
        return std::isfinite(value) && value >= minimum;
    };
    if (!finiteAtLeast(o.sizeScale, 0.001f) ||
        !finiteAtLeast(o.amountScale, 0.0f) ||
        !finiteAtLeast(o.lifetimeScale, 0.001f) ||
        !finiteAtLeast(o.speedScale, 0.0f) ||
        !finiteAtLeast(o.radiusScale, 0.001f) ||
        !finiteAtLeast(o.emitterRadius, 0.0f))
        return false;
    for (int i = 0; i < 4; ++i)
        if (!finiteAtLeast(o.colourTint[i], 0.0f)) return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(o.localOffset[i])) return false;
    return true;
}

glm::mat4 toGlm(const Ogre::Affine3& m)
{
    glm::mat4 out(1.0f);
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            out[col][row] = float(m[row][col]);
    return out;
}

// The authored description is trusted for intent but not for values: a hand
// edited TOML can carry a negative lifetime or a zero-length direction, and the
// simulation must not have to defend against that on every particle.
ParticleEffectDesc sanitize(const ParticleEffectDesc& desc)
{
    ParticleEffectDesc clean = desc;
    clean.baseWidth = std::max(0.001f, clean.baseWidth);
    clean.baseHeight = std::max(0.001f, clean.baseHeight);
    clean.quota = std::max(1, clean.quota);
    clean.burstCount = std::max(0.0f, clean.burstCount);
    clean.qualityWeight = std::clamp(clean.qualityWeight, 0.0f, 1.0f);
    clean.hueJitter = std::clamp(clean.hueJitter, 0.0f, 1.0f);
    clean.scaleJitter = std::clamp(clean.scaleJitter, 0.0f, 0.95f);
    clean.drag = std::isfinite(clean.drag) ? std::max(0.0f, clean.drag) : 0.0f;
    clean.restitution = std::clamp(clean.restitution, 0.0f, 1.0f);
    clean.friction = std::clamp(clean.friction, 0.0f, 1.0f);
    for (auto& emitter : clean.emitters) {
        emitter.boxSize = glm::max(emitter.boxSize, glm::vec3(0.001f));
        emitter.angleDegrees = std::clamp(emitter.angleDegrees, 0.0f, 180.0f);
        emitter.emissionRate = std::max(0.0f, emitter.emissionRate);
        emitter.ttlMin = std::max(0.001f, emitter.ttlMin);
        emitter.ttlMax = std::max(0.001f, emitter.ttlMax);
        emitter.velocityMin = std::max(0.0f, emitter.velocityMin);
        emitter.velocityMax = std::max(0.0f, emitter.velocityMax);
        if (emitter.ttlMin > emitter.ttlMax)
            std::swap(emitter.ttlMin, emitter.ttlMax);
        if (emitter.velocityMin > emitter.velocityMax)
            std::swap(emitter.velocityMin, emitter.velocityMax);
        if (glm::dot(emitter.direction, emitter.direction) < 1e-8f)
            emitter.direction = {0.0f, 1.0f, 0.0f};
        else
            emitter.direction = glm::normalize(emitter.direction);
    }
    auto sortStops = [](auto& stops) {
        for (auto& stop : stops) stop.t = std::clamp(stop.t, 0.0f, 1.0f);
        std::stable_sort(stops.begin(), stops.end(),
                         [](const auto& a, const auto& b) { return a.t < b.t; });
    };
    sortStops(clean.colourRamp);
    sortStops(clean.sizeRamp);
    return clean;
}

} // namespace

void Particles::init(Ogre::SceneManager* sm)
{
    mSm = sm;
    mSim.reserve(kPoolCapacity);
    // Textures are scanned before any effect registers, so an effect naming a
    // stem finds its generated material already in place.
    mMaterials.load();
    mDecals.attach(sm);
}

// --- effects ---------------------------------------------------------------

std::string Particles::resolveMaterial(const ParticleEffectDesc& desc,
                                       FlipbookDesc& flipbookOut,
                                       ParticleBlend& blendOut) const
{
    flipbookOut = FlipbookDesc{};
    blendOut = ParticleBlend::Alpha;

    // A texture stem wins over an explicit material: it is the data-driven
    // path, and it carries blend and flipbook metadata the material alone
    // cannot express.
    if (!desc.texture.empty()) {
        if (const ParticleTextureDesc* tex = mMaterials.find(desc.texture)) {
            flipbookOut = tex->flipbook;
            blendOut = tex->blend;
            return particleAutoMaterialName(tex->stem);
        }
        log::warn("Particles: effect '%s' names unknown texture '%s'",
                  desc.name.c_str(), desc.texture.c_str());
    }

    if (!desc.material.empty() &&
        Ogre::MaterialManager::getSingleton().getByName(desc.material))
        return desc.material;

    if (!desc.material.empty())
        log::error("Particles: effect '%s' material '%s' is missing; using '%s'",
                   desc.name.c_str(), desc.material.c_str(),
                   prototype::kParticleMaterial);
    return prototype::kParticleMaterial;
}

size_t Particles::batchFor(const std::string& material,
                           ParticleRenderMode mode, ParticleBlend blend,
                           size_t capacity)
{
    for (size_t i = 0; i < mBatches.size(); ++i) {
        if (mBatches[i].material == material && mBatches[i].mode == mode)
            return i;
    }

    ParticleBatchDesc desc;
    desc.material = material;
    desc.mode = mode;
    desc.blend = blend;
    desc.capacity = std::max<size_t>(64, capacity);
    Batch entry;
    entry.material = material;
    entry.mode = mode;
    entry.blend = blend;
    entry.batch = createParticleBatch(mSm, desc);
    mBatches.push_back(std::move(entry));
    return mBatches.size() - 1;
}

void Particles::resizeBatches()
{
    // Every effect resolving to the same (material, mode) pushes into one
    // instance stream in the same frame, so a batch must hold the SUM of its
    // effects' demands. Sizing it to the largest single demand silently drops
    // whichever effect fills it second: push() clamps instead of overflowing,
    // so the loss shows up only as particles that never appear.
    //
    // Recomputed from mEffects rather than accumulated as effects register,
    // because hot-reload re-registers an existing effect and an accumulator
    // would grow without bound every time the TOML is saved.
    std::vector<size_t> wanted(mBatches.size(), 0);
    for (const Effect& e : mEffects) {
        if (e.batch < wanted.size())
            wanted[e.batch] += size_t(std::max(1, e.desc.quota)) * 2;
    }
    for (size_t i = 0; i < mBatches.size(); ++i) {
        const size_t need = std::max<size_t>(64, wanted[i]);
        if (mBatches[i].batch && mBatches[i].batch->capacity() < need)
            mBatches[i].batch->setCapacity(need);
    }
}

ParticleEffectId Particles::registerEffect(const ParticleEffectDesc& desc)
{
    if (!mSm || desc.name.empty() || desc.emitters.empty()) {
        log::error("Particles: effect requires a name and at least one emitter");
        return {};
    }

    ParticleEffectDesc clean = sanitize(desc);
    FlipbookDesc flipbook;
    ParticleBlend blend = ParticleBlend::Alpha;
    const std::string material = resolveMaterial(clean, flipbook, blend);
    clean.material = material;

    // Voxels are solids and never additive; the mode picks the material family.
    const size_t batch =
        batchFor(material, clean.renderMode, blend, size_t(clean.quota) * 2);

    for (size_t i = 0; i < mEffects.size(); ++i) {
        if (mEffects[i].desc.name != clean.name)
            continue;
        // Re-registration is how hot-reload lands: the id and the sim slot stay
        // put so live instances keep pointing at something valid, and only the
        // description under them changes.
        mSim.updateEffect(mEffects[i].simEffect, clean);
        mEffects[i].desc = std::move(clean);
        mEffects[i].batch = batch;
        mEffects[i].flipbook = flipbook;
        resizeBatches();
        return ParticleEffectId{uint32_t(i + 1)};
    }

    Effect effect;
    effect.simEffect = mSim.registerEffect(clean);
    effect.batch = batch;
    effect.flipbook = flipbook;
    effect.desc = std::move(clean);
    mEffects.push_back(std::move(effect));
    const uint32_t id = uint32_t(mEffects.size());
    mByName[mEffects.back().desc.name] = id;
    resizeBatches();
    return ParticleEffectId{id};
}

ParticleEffectId Particles::find(const std::string& name) const
{
    auto it = mByName.find(name);
    return it == mByName.end() ? ParticleEffectId{}
                               : ParticleEffectId{it->second};
}

// --- spawning --------------------------------------------------------------

ParticlesHandle Particles::spawn(ParticleEffectId fx, Ogre::SceneNode* parent,
                                 glm::vec3 localPos, bool ownsParent)
{
    return spawn(fx, parent, localPos, ParticleSpawnOptions{}, ownsParent);
}

ParticlesHandle Particles::spawn(ParticleEffectId fx, Ogre::SceneNode* parent,
                                 glm::vec3 localPos,
                                 const ParticleSpawnOptions& rawOptions,
                                 bool ownsParent)
{
    if (!fx.valid() || fx.id > mEffects.size() || !parent) return {};
    Effect& e = mEffects[fx.id - 1];

    if (!validSpawnOptions(rawOptions))
        log::warn("Particles: invalid spawn options sanitized for effect '%s'",
                  e.desc.name.c_str());

    const float qscale = particleQualityScale(e.desc.visualRole,
                                              e.desc.qualityWeight, mQuality);
    ResolvedParticleSpawn resolved =
        resolveParticleSpawn(e.desc, rawOptions, localPos, qscale);

    // Per-spawn variety, seeded from the handle counter so repeated casts differ
    // while any single spawn stays reproducible.
    const uint32_t seed = mNextHandle * 2654435761u + 1u;
    if (e.desc.scaleJitter > 0.0f || e.desc.hueJitter > 0.0f) {
        ParticleRng rng(seed);
        if (e.desc.scaleJitter > 0.0f) {
            const float j = 1.0f + rng.signedUnit() * e.desc.scaleJitter;
            resolved.defaultWidth *= j;
            resolved.defaultHeight *= j;
        }
        if (e.desc.hueJitter > 0.0f) {
            const float j = rng.signedUnit() * e.desc.hueJitter;
            const auto shift = [j](glm::vec4& c) {
                c.r = std::clamp(c.r + j, 0.0f, 1.0f);
                c.b = std::clamp(c.b - j, 0.0f, 1.0f);
            };
            for (auto& stop : resolved.colourRamp) shift(stop.rgba);
            for (auto& emitter : resolved.emitters) shift(emitter.colour);
        }
    }

    const uint32_t instance = mSim.addInstance(
        e.simEffect, resolved, toGlm(parent->_getFullTransform()), seed);
    if (instance == ParticleSim::kInvalidInstance) {
        log::warn("Particles: no free instance slot for effect '%s'",
                  e.desc.name.c_str());
        return {};
    }

    Live lv;
    lv.effect = fx.id;
    lv.instance = instance;
    lv.parent = parent;
    lv.ownsParent = ownsParent;
    const uint32_t id = mNextHandle++;
    mLive[id] = lv;
    return ParticlesHandle{id};
}

void Particles::stop(ParticlesHandle h)
{
    auto it = mLive.find(h.id);
    if (it == mLive.end()) return;
    // Stop emitting but let the tail age out, which is what a fireball trail
    // needs when its projectile dies mid-flight.
    mSim.stopInstance(it->second.instance);
}

void Particles::despawn(ParticlesHandle h)
{
    auto it = mLive.find(h.id);
    if (it == mLive.end()) return;
    mSim.killInstance(it->second.instance);
    if (it->second.ownsParent && it->second.parent && mSm)
        mSm->destroySceneNode(it->second.parent);
    mLive.erase(it);
}

void Particles::setQuality(float q)
{
    mQuality = std::clamp(q, 0.25f, 1.0f);
}

// --- frame -----------------------------------------------------------------

void Particles::pushTransforms()
{
    // The sim is renderer-agnostic and cannot read a scene node, so the parent
    // transform is pushed to it once per frame. A moving emitter must be
    // current before emission or its particles trail a frame behind.
    for (auto& [id, lv] : mLive) {
        if (!lv.parent) continue;
        mSim.setInstanceTransform(lv.instance,
                                  toGlm(lv.parent->_getFullTransform()));
    }
}

glm::vec3 Particles::cameraPosition() const
{
    if (!mSm) return glm::vec3(0.0f);
    const auto& cameras = mSm->getCameras();
    if (cameras.empty()) return glm::vec3(0.0f);
    const Ogre::Vector3 p = cameras.begin()->second->getDerivedPosition();
    return {p.x, p.y, p.z};
}

void Particles::fillBatches()
{
    for (Batch& b : mBatches)
        if (b.batch) b.batch->beginFrame();

    const ParticlePool& pool = mSim.pool();
    for (size_t i = 0; i < mEffects.size(); ++i) {
        const Effect& e = mEffects[i];
        IParticleBatch* batch = mBatches[e.batch].batch.get();
        if (!batch) continue;

        const std::vector<uint32_t>& indices =
            mSim.indicesForEffect(e.simEffect);
        if (indices.empty()) continue;

        mStaging.clear();
        mStaging.reserve(indices.size());
        const float frames =
            e.flipbook.active() ? float(e.flipbook.frameCount()) : 0.0f;
        for (uint32_t p : indices) {
            ParticleInstance inst;
            inst.pos = pool.pos[p];
            inst.size = pool.size[p];
            inst.colour = pool.colour[p];
            inst.rot = pool.rot[p];
            inst.frame = 0.0f;
            if (frames > 0.0f) {
                // Walk the sheet on wall-clock time rather than on normalised
                // life, so a flipbook keeps its authored cadence no matter how
                // long the particle happens to live.
                const float f = pool.age[p] * e.flipbook.fps;
                inst.frame = e.flipbook.loop
                    ? std::fmod(f, frames)
                    : std::min(f, frames - 1.0f);
            }
            mStaging.push_back(inst);
        }
        batch->push(mStaging.data(), mStaging.size());
    }

    const glm::vec3 eye = cameraPosition();
    for (Batch& b : mBatches) {
        if (!b.batch) continue;
        if (b.batch->needsSorting()) b.batch->sort(eye);
        b.batch->endFrame();
    }
}

std::vector<uint32_t> Particles::update(float dt)
{
    pushTransforms();

    mDecalRequests.clear();
    mSim.update(dt, mDecalRequests);
    for (const DecalRequest& request : mDecalRequests)
        mDecals.spawn(request);
    mDecals.update(dt, cameraPosition());

    fillBatches();

    // A handle is retired once its instance has stopped emitting and drained.
    // Callers rely on this to release whatever they attached the effect to.
    std::vector<uint32_t> done;
    for (const auto& [id, lv] : mLive)
        if (!mSim.instanceActive(lv.instance))
            done.push_back(id);
    for (uint32_t id : done) despawn(ParticlesHandle{id});
    return done;
}

void Particles::shutdown()
{
    clear();
    mDecals.shutdown();
    mSm = nullptr;
}

void Particles::clear()
{
    // Renderer::clearScene destroys the scene graph around us, so drop every
    // reference into it before those nodes go away.
    mLive.clear();
    mSim.clear();
    mDecals.clear();
    for (Batch& b : mBatches)
        if (b.batch) b.batch->destroy();
    mBatches.clear();
    mEffects.clear();
    mByName.clear();
}

} // namespace eng
