#include "Particles.h"
#include <eng/Log.h>
#include <OgreSceneManager.h>
#include <OgreParticleSystem.h>
#include <OgreParticleEmitter.h>
#include <OgreParticleAffector.h>
#include <OgreSceneNode.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace eng {
namespace {
std::string f2s(float v){ return std::to_string(v); }
std::string v3s(glm::vec3 v){ return f2s(v.x)+" "+f2s(v.y)+" "+f2s(v.z); }
std::string c4s(glm::vec4 c){ return f2s(c.r)+" "+f2s(c.g)+" "+f2s(c.b)+" "+f2s(c.a); }
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
    ps->setMaterialName(d.material);
    ps->setDefaultDimensions(d.baseWidth, d.baseHeight);
    // setRenderer exists (verified in OgreParticleSystem.h line 86). Uses the
    // string "billboard" which is the built-in Ogre billboard renderer type.
    ps->setRenderer("billboard");
    // billboard_type via StringInterface setParameter (matches .particle script keys).
    ps->setParameter("billboard_type", "point");
    applyQuota(ps, d);
    const float qscale = 1.0f - d.qualityWeight * (1.0f - mQuality);
    for (const auto& e : d.emitters){
        // addEmitter("Point") — built-in point emitter type.
        Ogre::ParticleEmitter* em = ps->addEmitter("Point");
        em->setParameter("angle", f2s(e.angleDegrees));
        em->setParameter("direction", v3s(e.direction));
        if (d.loop){
            em->setParameter("emission_rate", f2s(e.emissionRate * qscale));
        } else {
            // One-shot: burst over a short window so Ogre sees a finite emission_rate.
            const float window = 0.05f;
            em->setParameter("emission_rate", f2s(std::max(1.0f, d.burstCount * qscale)/window));
            em->setParameter("duration", f2s(window));
        }
        em->setParameter("time_to_live_min", f2s(e.ttlMin));
        em->setParameter("time_to_live_max", f2s(e.ttlMax));
        em->setParameter("velocity_min", f2s(e.velocityMin));
        em->setParameter("velocity_max", f2s(e.velocityMax));
        em->setParameter("colour", d.colourRamp.empty() ? c4s(e.startColour) : c4s(d.colourRamp.front().rgba));
    }
    if (!d.colourRamp.empty()){
        // ColourInterpolator affector supports up to 6 time/colour pairs (time0..time5).
        Ogre::ParticleAffector* af = ps->addAffector("ColourInterpolator");
        const int n = std::min<int>(6, int(d.colourRamp.size()));
        for (int i=0;i<n;++i){
            af->setParameter("time"+std::to_string(i), f2s(d.colourRamp[i].t));
            af->setParameter("colour"+std::to_string(i), c4s(d.colourRamp[i].rgba));
        }
    }
    if (d.sizeRamp.size() >= 2){
        // Scaler affector takes a constant rate (units/sec) applied to particle size.
        // Approximate from first emitter average TTL and overall scale delta.
        const float avgTtl = d.emitters.empty()?0.5f:0.5f*(d.emitters[0].ttlMin+d.emitters[0].ttlMax);
        const float baseDim = 0.5f*(d.baseWidth+d.baseHeight);
        const float rate = (d.sizeRamp.back().scale - d.sizeRamp.front().scale)*baseDim/std::max(0.01f,avgTtl);
        ps->addAffector("Scaler")->setParameter("rate", f2s(rate));
    }
    if (d.rotationJitterDeg > 0.0f){
        // Rotator affector: random initial rotation in [0,360] + random spin speed.
        Ogre::ParticleAffector* rot = ps->addAffector("Rotator");
        rot->setParameter("rotation_range_start","0");
        rot->setParameter("rotation_range_end","360");
        rot->setParameter("rotation_speed_range_start", f2s(-d.rotationJitterDeg));
        rot->setParameter("rotation_speed_range_end", f2s(d.rotationJitterDeg));
    }
    ps->setEmitting(false);
    return ps;
}

ParticleEffectId Particles::registerEffect(const ParticleEffectDesc& desc){
    for (size_t i=0;i<mEffects.size();++i){
        if (mEffects[i].desc.name == desc.name){
            // Re-register: flush pool for this effect so stale built systems are gone.
            for (auto* ps : mEffects[i].free) mSm->destroyParticleSystem(ps);
            mEffects[i].free.clear();
            mEffects[i].desc = desc;
            return ParticleEffectId{ uint32_t(i+1) };
        }
    }
    mEffects.push_back({ desc, {} });
    return ParticleEffectId{ uint32_t(mEffects.size()) };
}

ParticlesHandle Particles::spawn(ParticleEffectId fx, Ogre::SceneNode* parent, glm::vec3 localPos){
    if (!fx.valid() || fx.id > mEffects.size() || !parent) return {};
    Effect& e = mEffects[fx.id-1];
    Ogre::ParticleSystem* ps = nullptr;
    if (!e.free.empty()){ ps = e.free.back(); e.free.pop_back(); }
    else { ps = build(e.desc); }
    parent->attachObject(ps);
    // getParentSceneNode() returns the node we just attached to.
    if (ps->getParentSceneNode()) ps->getParentSceneNode()->setPosition(localPos.x, localPos.y, localPos.z);
    ps->clear();
    for (unsigned short i=0;i<ps->getNumEmitters();++i) ps->getEmitter(i)->setEnabled(true);
    ps->setEmitting(true);
    float maxTtl = 0.0f;
    for (const auto& em : e.desc.emitters) maxTtl = std::max(maxTtl, em.ttlMax);
    Live lv; lv.effect = fx.id; lv.ps = ps; lv.oneShot = !e.desc.loop;
    lv.maxLife = 0.05f + maxTtl; lv.active = true;
    const uint32_t id = mNextHandle++;
    mLive[id] = lv;
    return ParticlesHandle{ id };
}

void Particles::stop(ParticlesHandle h){
    auto it = mLive.find(h.id); if (it==mLive.end()) return;
    it->second.ps->setEmitting(false);
    it->second.oneShot = true; it->second.age = 0.0f;
    float maxTtl = 0.0f;
    for (const auto& em : mEffects[it->second.effect-1].desc.emitters) maxTtl = std::max(maxTtl, em.ttlMax);
    it->second.maxLife = maxTtl;
}

void Particles::despawn(ParticlesHandle h){
    auto it = mLive.find(h.id); if (it==mLive.end()) return;
    Ogre::ParticleSystem* ps = it->second.ps;
    ps->setEmitting(false); ps->clear();
    // detachObject(MovableObject*) verified in OgreSceneNode.h line 151.
    if (ps->getParentSceneNode()) ps->getParentSceneNode()->detachObject(ps);
    mEffects[it->second.effect-1].free.push_back(ps);
    mLive.erase(it);
}

void Particles::setQuality(float q){
    mQuality = std::clamp(q, 0.25f, 1.0f);
    // Flush all free pools so next build() uses updated quota.
    for (auto& e : mEffects){ for (auto* ps : e.free) mSm->destroyParticleSystem(ps); e.free.clear(); }
}

void Particles::update(float dt){
    std::vector<uint32_t> done;
    for (auto& [id, lv] : mLive){
        if (!lv.oneShot) continue;
        lv.age += dt;
        if (lv.age >= lv.maxLife && lv.ps->getNumParticles()==0) done.push_back(id);
    }
    for (uint32_t id : done) despawn(ParticlesHandle{ id });
}

void Particles::clear(){
    // Called by Renderer::clearScene before destroyAllParticleSystems, so we just
    // drop our bookkeeping; Ogre frees the actual systems.
    mLive.clear();
    for (auto& e : mEffects) e.free.clear();
}

} // namespace eng
