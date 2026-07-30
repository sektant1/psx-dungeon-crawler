#include "Particles.h"
#include <eng/Log.h>
#include <eng/render/PrototypeAssets.h>
#include <OgreSceneManager.h>
#include <OgreParticleSystem.h>
#include <OgreParticleEmitter.h>
#include <OgreParticleAffector.h>
#include <OgreMaterialManager.h>
#include <OgreSceneNode.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace eng {
namespace {
constexpr float kNonVisibleUpdateTimeout = 0.25f;

std::string f2s(float v){ return std::to_string(v); }
std::string v3s(glm::vec3 v){ return f2s(v.x)+" "+f2s(v.y)+" "+f2s(v.z); }
std::string c4s(glm::vec4 c){ return f2s(c.r)+" "+f2s(c.g)+" "+f2s(c.b)+" "+f2s(c.a); }

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
}

void Particles::init(Ogre::SceneManager* sm){ mSm = sm; }

void Particles::applyQuota(Ogre::ParticleSystem* ps, const ParticleEffectDesc& d){
    const float qscale = 1.0f - d.qualityWeight * (1.0f - mQuality);
    const int quota = std::max(1, int(std::lround(d.quota * qscale)));
    ps->setParameter("quota", std::to_string(quota));
}

Ogre::ParticleSystem* Particles::build(const ParticleEffectDesc& d){
    const std::string name = "fx_" + d.name + "_" + std::to_string(mNextName++);
    // createParticleSystem(name, quota) — blank system, no template.
    Ogre::ParticleSystem* ps = mSm->createParticleSystem(name, std::max(1, d.quota));
    // Stop emitter/affector simulation shortly after a system is culled by its
    // node or camera. Ogre automatically resumes updates on visibility and
    // resets the timer, preserving pooled and per-spawn behavior.
    ps->setNonVisibleUpdateTimeout(kNonVisibleUpdateTimeout);
    ps->setMaterialName(d.material);
    ps->setDefaultDimensions(d.baseWidth, d.baseHeight);
    // World space so a moving emitter (fireball) leaves particles behind as a
    // trail instead of dragging them along with it.
    ps->setKeepParticlesInLocalSpace(d.localSpace);
    // setRenderer exists (verified in OgreParticleSystem.h line 86). Uses the
    // string "billboard" which is the built-in Ogre billboard renderer type.
    ps->setRenderer("billboard");
    // billboard_type via StringInterface setParameter (matches .particle script keys).
    ps->setParameter("billboard_type", "point");
    applyQuota(ps, d);
    ps->setEmitting(false);
    return ps;
}

void Particles::configure(Ogre::ParticleSystem* ps,
                          const ParticleEffectDesc& d,
                          const ResolvedParticleSpawn& resolved)
{
    ps->setMaterialName(d.material);
    ps->setKeepParticlesInLocalSpace(d.localSpace);
    applyQuota(ps, d);
    ps->setDefaultDimensions(resolved.defaultWidth, resolved.defaultHeight);
    ps->removeAllEmitters();
    ps->removeAllAffectors();

    for (const ResolvedParticleEmitter& emitter : resolved.emitters) {
        Ogre::ParticleEmitter* em = ps->addEmitter(
            emitter.shape == ParticleEmitterShape::Box ? "Box" : "Point");
        em->setEnabled(true);
        if (emitter.shape == ParticleEmitterShape::Box) {
            em->setParameter("width", f2s(emitter.boxSize.x));
            em->setParameter("height", f2s(emitter.boxSize.y));
            em->setParameter("depth", f2s(emitter.boxSize.z));
        }
        em->setParameter("position", v3s(emitter.position));
        em->setParameter("angle", f2s(emitter.angleDegrees));
        em->setParameter("direction", v3s(emitter.direction));
        em->setParameter("emission_rate", f2s(emitter.emissionRate));
        if (emitter.emissionDuration > 0.0f)
            em->setParameter("duration", f2s(emitter.emissionDuration));
        em->setParameter("time_to_live_min", f2s(emitter.ttlMin));
        em->setParameter("time_to_live_max", f2s(emitter.ttlMax));
        em->setParameter("velocity_min", f2s(emitter.velocityMin));
        em->setParameter("velocity_max", f2s(emitter.velocityMax));
        em->setParameter("colour", c4s(emitter.colour));
    }
    if (!resolved.colourRamp.empty()){
        // ColourInterpolator affector supports up to 6 time/colour pairs (time0..time5).
        Ogre::ParticleAffector* af = ps->addAffector("ColourInterpolator");
        const int n = std::min<int>(6, int(resolved.colourRamp.size()));
        for (int i=0;i<n;++i){
            af->setParameter("time"+std::to_string(i),
                             f2s(resolved.colourRamp[i].t));
            af->setParameter("colour"+std::to_string(i),
                             c4s(resolved.colourRamp[i].rgba));
        }
    }
    if (d.sizeRamp.size() >= 2){
        // Scaler affector takes a constant rate (units/sec) applied to particle size.
        // Approximate from first emitter average TTL and overall scale delta.
        ps->addAffector("Scaler")->setParameter(
            "rate", f2s(resolved.scalerRate));
    }
    if (d.rotationJitterDeg > 0.0f){
        // Rotator affector: random initial rotation in [0,360] + random spin speed.
        Ogre::ParticleAffector* rot = ps->addAffector("Rotator");
        rot->setParameter("rotation_range_start","0");
        rot->setParameter("rotation_range_end","360");
        rot->setParameter("rotation_speed_range_start", f2s(-d.rotationJitterDeg));
        rot->setParameter("rotation_speed_range_end", f2s(d.rotationJitterDeg));
    }
    if (glm::dot(d.acceleration, d.acceleration) > 1e-8f) {
        Ogre::ParticleAffector* force = ps->addAffector("LinearForce");
        force->setParameter("force_vector", v3s(d.acceleration));
        force->setParameter("force_application", "add");
    }
    ps->setEmitting(false);
}

ParticleEffectId Particles::registerEffect(const ParticleEffectDesc& desc){
    if (!mSm || desc.name.empty() || desc.material.empty() || desc.emitters.empty()) {
        log::error("Particles: effect requires a name, material, and emitter");
        return {};
    }

    ParticleEffectDesc clean = desc;
    if (!Ogre::MaterialManager::getSingleton().getByName(clean.material)) {
        log::error("Particles: effect '%s' material '%s' is missing; using '%s'",
                   clean.name.c_str(), clean.material.c_str(),
                   prototype::kParticleMaterial);
        clean.material = prototype::kParticleMaterial;
    }
    clean.baseWidth = std::max(0.001f, clean.baseWidth);
    clean.baseHeight = std::max(0.001f, clean.baseHeight);
    clean.quota = std::max(1, clean.quota);
    clean.burstCount = std::max(0.0f, clean.burstCount);
    clean.qualityWeight = std::clamp(clean.qualityWeight, 0.0f, 1.0f);
    clean.hueJitter = std::clamp(clean.hueJitter, 0.0f, 1.0f);
    clean.scaleJitter = std::clamp(clean.scaleJitter, 0.0f, 0.95f);
    for (auto& emitter : clean.emitters) {
        emitter.boxSize = glm::max(emitter.boxSize, glm::vec3(0.001f));
        emitter.angleDegrees = std::clamp(emitter.angleDegrees, 0.0f, 180.0f);
        emitter.emissionRate = std::max(0.0f, emitter.emissionRate);
        emitter.ttlMin = std::max(0.001f, emitter.ttlMin);
        emitter.ttlMax = std::max(0.001f, emitter.ttlMax);
        emitter.velocityMin = std::max(0.0f, emitter.velocityMin);
        emitter.velocityMax = std::max(0.0f, emitter.velocityMax);
        if (emitter.ttlMin > emitter.ttlMax) std::swap(emitter.ttlMin, emitter.ttlMax);
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

    for (size_t i=0;i<mEffects.size();++i){
        if (mEffects[i].desc.name == clean.name){
            // Re-register: flush pool for this effect so stale built systems are gone.
            for (auto* ps : mEffects[i].free) mSm->destroyParticleSystem(ps);
            mEffects[i].free.clear();
            mEffects[i].desc = std::move(clean);
            ++mEffects[i].generation;
            return ParticleEffectId{ uint32_t(i+1) };
        }
    }
    mEffects.push_back({ std::move(clean), {}, 1 });
    const uint32_t id = uint32_t(mEffects.size());
    mByName[mEffects.back().desc.name] = id;
    return ParticleEffectId{ id };
}

ParticleEffectId Particles::find(const std::string& name) const {
    auto it = mByName.find(name);
    return it == mByName.end() ? ParticleEffectId{} : ParticleEffectId{ it->second };
}

ParticlesHandle Particles::spawn(ParticleEffectId fx, Ogre::SceneNode* parent,
                                 glm::vec3 localPos, bool ownsParent){
    return spawn(fx, parent, localPos, ParticleSpawnOptions{}, ownsParent);
}

ParticlesHandle Particles::spawn(ParticleEffectId fx, Ogre::SceneNode* parent,
                                 glm::vec3 localPos,
                                 const ParticleSpawnOptions& rawOptions,
                                 bool ownsParent){
    if (!fx.valid() || fx.id > mEffects.size() || !parent) return {};
    Effect& e = mEffects[fx.id-1];
    Ogre::ParticleSystem* ps = nullptr;
    if (!e.free.empty()){ ps = e.free.back(); e.free.pop_back(); }
    else { ps = build(e.desc); }
    // Defensive: a pooled system must be detached before re-attaching (Ogre
    // forbids attaching a MovableObject to two nodes). The particle inherits the
    // parent's world transform. Renderer-owned legacy offsets use a child node;
    // direct callers and ParticleSpawnOptions use emitter-local positions.
    if (ps->getParentSceneNode()) ps->getParentSceneNode()->detachObject(ps);
    parent->attachObject(ps);
    ps->clear();
    if (!validSpawnOptions(rawOptions))
        log::warn("Particles: invalid spawn options sanitized for effect '%s'",
                  e.desc.name.c_str());
    const float qscale =
        1.0f - e.desc.qualityWeight * (1.0f - mQuality);
    const ResolvedParticleSpawn resolved =
        resolveParticleSpawn(e.desc, rawOptions, localPos, qscale);
    configure(ps, e.desc, resolved);
    const ParticleSpawnOptions& options = resolved.options;
    const float initialScale =
        e.desc.sizeRamp.empty() ? 1.0f : e.desc.sizeRamp.front().scale;

    // Per-spawn variety: seed a cheap deterministic RNG from the handle counter so
    // repeated casts differ but a given spawn is reproducible.
    const ParticleEffectDesc& d = e.desc;
    if (d.scaleJitter > 0.0f || d.hueJitter > 0.0f){
        uint32_t s = mNextHandle * 2654435761u + 1u;
        auto rnd = [&s]{ s ^= s<<13; s ^= s>>17; s ^= s<<5;
                         return (s & 0xffffff) / float(0xffffff); }; // 0..1
        if (d.scaleJitter > 0.0f){
            const float j = 1.0f + (rnd()*2.0f - 1.0f) * d.scaleJitter;
            ps->setDefaultDimensions(
                d.baseWidth * initialScale * options.sizeScale * j,
                d.baseHeight * initialScale * options.sizeScale * j);
        }
        if (d.hueJitter > 0.0f){
            const float j = (rnd()*2.0f - 1.0f) * d.hueJitter;
            for (unsigned short i=0;i<ps->getNumEmitters();++i) {
                glm::vec4 c = d.colourRamp.empty()
                    ? d.emitters[std::min<size_t>(i, d.emitters.size()-1)].startColour
                    : d.colourRamp.front().rgba;
                c *= options.colourTint;
                c.r = std::clamp(c.r + j, 0.0f, 1.0f);
                c.b = std::clamp(c.b - j, 0.0f, 1.0f);
                ps->getEmitter(i)->setParameter("colour", c4s(c));
            }
        }
    }

    ps->setEmitting(true);
    Live lv; lv.effect = fx.id; lv.ps = ps; lv.oneShot = !e.desc.loop;
    lv.maxLife = 0.05f + resolved.maxTtl; lv.active = true;
    lv.particleLife = resolved.maxTtl;
    lv.ownsParent = ownsParent; lv.generation = e.generation;
    const uint32_t id = mNextHandle++;
    mLive[id] = lv;
    return ParticlesHandle{ id };
}

void Particles::stop(ParticlesHandle h){
    auto it = mLive.find(h.id); if (it==mLive.end()) return;
    it->second.ps->setEmitting(false);
    it->second.oneShot = true; it->second.age = 0.0f;
    it->second.maxLife = it->second.particleLife;
}

void Particles::despawn(ParticlesHandle h){
    auto it = mLive.find(h.id); if (it==mLive.end()) return;
    Ogre::ParticleSystem* ps = it->second.ps;
    ps->setEmitting(false); ps->clear();
    // detachObject(MovableObject*) verified in OgreSceneNode.h line 151.
    Ogre::SceneNode* parent = ps->getParentSceneNode();
    if (parent) parent->detachObject(ps);
    Effect& effect = mEffects[it->second.effect-1];
    if (it->second.generation == effect.generation)
        effect.free.push_back(ps);
    else
        mSm->destroyParticleSystem(ps);
    if (it->second.ownsParent && parent)
        mSm->destroySceneNode(parent);
    mLive.erase(it);
}

void Particles::setQuality(float q){
    mQuality = std::clamp(q, 0.25f, 1.0f);
    // Flush all free pools so next build() uses updated quota.
    for (auto& e : mEffects){ for (auto* ps : e.free) mSm->destroyParticleSystem(ps); e.free.clear(); }
}

std::vector<uint32_t> Particles::update(float dt){
    std::vector<uint32_t> done;
    for (auto& [id, lv] : mLive){
        if (!lv.oneShot) continue;
        lv.age += dt;
        if (particleSystemLifetimeExpired(
                lv.oneShot, lv.age, lv.maxLife))
            done.push_back(id);
    }
    for (uint32_t id : done) despawn(ParticlesHandle{ id });
    return done;
}

void Particles::clear(){
    // Called by Renderer::clearScene before destroyAllParticleSystems, so we just
    // drop our bookkeeping; Ogre frees the actual systems.
    mLive.clear();
    for (auto& e : mEffects) e.free.clear();
}

} // namespace eng
