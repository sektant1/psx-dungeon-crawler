#include "Spells.h"

#include "CombatConfig.h"

#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>

// Convert an sRGB-picked colour into the linear, energy-premultiplied form the
// renderer expects for LightDesc.colour.
static glm::vec3 lightEnergy(glm::vec3 srgb, float gain = 2.0f) {
    return glm::pow(glm::max(srgb, glm::vec3(0.0f)), glm::vec3(2.2f)) * gain;
}

// ---- helpers ----

// Build a quaternion rotating fromDir to toDir (copied from Projectiles.cpp),
// used to orient the stretched beam box (+Y long axis) along the aim direction.
static glm::quat rotateFromTo(glm::vec3 from, glm::vec3 to) {
    from = glm::normalize(from);
    to   = glm::normalize(to);
    float d = glm::dot(from, to);
    if (d > 0.9999f) return glm::quat(1, 0, 0, 0);
    if (d < -0.9999f) {
        glm::vec3 perp = glm::normalize(
            std::fabs(from.x) < 0.9f ? glm::cross(from, glm::vec3(1, 0, 0))
                                      : glm::cross(from, glm::vec3(0, 1, 0)));
        return glm::angleAxis(glm::pi<float>(), perp);
    }
    glm::vec3 axis = glm::cross(from, to);
    float w = 1.0f + d;
    return glm::normalize(glm::quat(w, axis.x, axis.y, axis.z));
}

// ---- init ----

void SpellSystem::init(eng::Renderer& r) {
    mFireballMesh = r.createSphere(0.14f, 10, 14); // glowing spherical core
    mBeamMesh     = r.createBeveledBox(0.02f);     // thin segment, scaled per cast
}

// ---- transient bursts ----

eng::NodeHandle SpellSystem::spawnBurst(eng::Renderer& r, glm::vec3 pos,
                                        const std::string& particleName, float ttl,
                                        glm::vec3 lightColour, float lightRange) {
    eng::NodeHandle n = r.createNode(eng::kRootNode, pos);
    r.attachParticles(n, particleName);
    if (lightRange > 0.0f) {
        eng::LightDesc l;
        l.colour = lightEnergy(lightColour);
        l.range  = lightRange;
        r.attachLight(n, l);
    }
    mTransients.push_back({ n, ttl });
    return n;
}

// ---- fireball ----

void SpellSystem::castFireball(eng::Physics& phys, eng::Renderer& r,
                               glm::vec3 eye, glm::vec3 fwd) {
    if (!mCfg) return;
    const CombatConfig::Fireball& fb = mCfg->fireball;

    if (int(mFireballs.size()) >= mMaxFireballs) {
        despawnFireball(phys, r, mFireballs.front());
        mFireballs.erase(mFireballs.begin());
    }

    fwd = glm::normalize(fwd);
    glm::vec3 spawn = eye + fwd * 0.6f;

    eng::BodyDesc d;
    d.kind          = eng::ShapeKind::Sphere;
    d.radius        = fb.radius;
    d.position      = spawn;
    d.layer         = eng::BodyLayer::Projectile;
    d.dynamic       = true;
    d.continuousCast = true;
    d.mass          = fb.mass;
    d.restitution   = 0.0f;

    eng::BodyHandle body = phys.createBody(d);
    if (!body.valid()) return;

    phys.applyImpulse(body, fwd * (fb.mass * fb.speed), spawn);

    eng::NodeHandle node = r.createNode(eng::kRootNode, spawn);
    r.attachMesh(node, mFireballMesh, fb.trailParticle.c_str(), false);
    r.attachParticles(node, fb.trailParticle);
    if (fb.lightRange > 0.0f) {
        eng::LightDesc l;
        l.colour = lightEnergy(fb.lightColour);
        l.range  = fb.lightRange;
        r.attachLight(node, l);
    }

    spawnBurst(r, spawn, fb.muzzleParticle, 0.25f, fb.lightColour, fb.lightRange);

    mFireballs.push_back({ body, node, fb.ttl, false });
}

// ---- beam (hitscan) ----

void SpellSystem::castBeam(eng::Physics& phys, eng::Renderer& r,
                           glm::vec3 eye, glm::vec3 fwd) {
    if (!mCfg) return;
    const CombatConfig::Beam& bm = mCfg->beam;

    fwd = glm::normalize(fwd);
    const float range = bm.range;

    // rayCast takes a single BodyLayer mask; query Prop and Static, keep nearer.
    eng::RayHit hitProp, hitStatic;
    bool hp = phys.rayCast(eye, fwd, range, hitProp,   eng::BodyLayer::Prop);
    bool hs = phys.rayCast(eye, fwd, range, hitStatic, eng::BodyLayer::Static);

    glm::vec3 endPt = eye + fwd * range;
    bool struck = false;
    eng::RayHit chosen;
    if (hp && (!hs || hitProp.fraction <= hitStatic.fraction)) { chosen = hitProp;   struck = true; }
    else if (hs)                                               { chosen = hitStatic; struck = true; }
    if (struck) endPt = chosen.point;

    float len = glm::length(endPt - eye);

    // Visual: a thin box stretched from eye to endPt for a few frames.
    glm::vec3 mid = (eye + endPt) * 0.5f;
    eng::NodeHandle beam = r.createNode(eng::kRootNode, mid);
    r.setOrientation(beam, rotateFromTo(glm::vec3(0, 1, 0), fwd)); // box +Y -> fwd
    // mBeamMesh is a 1 m unit cube: y-scale = len spans the full eye->hit length.
    r.setScale(beam, glm::vec3(bm.width, len, bm.width));
    r.attachMesh(beam, mBeamMesh, bm.coreParticle.c_str(), false);
    mTransients.push_back({ beam, bm.segmentTtl });

    if (struck) {
        spawnBurst(r, endPt, bm.impactParticle, 0.25f, bm.lightColour, bm.lightRange);
        if (chosen.body.valid())
            phys.applyImpulse(chosen.body, fwd * bm.impulse, endPt);
    }
}

// ---- fireball impact (routed via contact callback) ----

void SpellSystem::onHit(eng::Physics& phys, eng::Renderer& r,
                        const eng::HitEvent& e) {
    if (!mCfg) return;
    for (auto& f : mFireballs) {
        if (!f.dead && (f.body == e.self || f.body == e.other)) {
            f.dead = true;
            f.ttl  = 0.0f;
            const CombatConfig::Fireball& fb = mCfg->fireball;
            spawnBurst(r, e.point, fb.impactParticle, 0.35f, fb.lightColour, fb.lightRange);
            eng::BodyHandle target = (f.body == e.self) ? e.other : e.self;
            if (target.valid())
                phys.applyImpulse(target, -e.normal * fb.impactImpulse, e.point);
            return;
        }
    }
}

// ---- lifetime ----

void SpellSystem::despawnFireball(eng::Physics& phys, eng::Renderer& r, Fireball& f) {
    phys.removeBody(f.body);
    r.setNodeVisible(f.node, false);
}

void SpellSystem::fixedUpdate(eng::Physics& phys, eng::Renderer& r, float dt) {
    for (auto& f : mFireballs) f.ttl -= dt;
    for (int i = int(mFireballs.size()) - 1; i >= 0; --i)
        if (mFireballs[i].ttl <= 0.0f) {
            despawnFireball(phys, r, mFireballs[i]);
            mFireballs.erase(mFireballs.begin() + i);
        }

    for (auto& t : mTransients) t.ttl -= dt;
    for (int i = int(mTransients.size()) - 1; i >= 0; --i)
        if (mTransients[i].ttl <= 0.0f) {
            r.setNodeVisible(mTransients[i].node, false);
            mTransients.erase(mTransients.begin() + i);
        }
}

void SpellSystem::syncRender(eng::Physics& phys, eng::Renderer& r) {
    for (auto& f : mFireballs) {
        glm::vec3 p;
        glm::quat q;
        phys.getRenderTransform(f.body, p, q);
        r.setPosition(f.node, p);
    }
}

void SpellSystem::clear(eng::Physics& phys, eng::Renderer& r) {
    for (auto& f : mFireballs) despawnFireball(phys, r, f);
    mFireballs.clear();
    for (auto& t : mTransients) r.setNodeVisible(t.node, false);
    mTransients.clear();
}
