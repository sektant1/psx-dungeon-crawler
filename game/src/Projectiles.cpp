#include "Projectiles.h"

#include <eng/Physics.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

// ---- helpers ----

// Build a quaternion rotating fromDir to toDir, used to orient projectile bodies.
static glm::quat rotateFromTo(glm::vec3 from, glm::vec3 to) {
    from = glm::normalize(from);
    to   = glm::normalize(to);
    float d = glm::dot(from, to);
    if (d > 0.9999f) return glm::quat(1, 0, 0, 0);
    if (d < -0.9999f) {
        // 180-degree rotation: pick any perpendicular axis
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

void ProjectileSystem::init(eng::Renderer& r) {
    // Arrow: thin cone (radius 0.03, height 0.5) to represent the shaft tip
    mArrowMesh = r.createCone(0.03f, 0.5f, 6);
    // Bolt: small beveled box for a magical projectile look
    mBoltMesh  = r.createBeveledBox(0.05f);
}

// ---- fire ----

void ProjectileSystem::fireArrow(eng::Physics& phys, eng::Renderer& r,
                                  glm::vec3 eye, glm::vec3 fwd) {
    // Enforce max live limit: despawn oldest arrow if over cap
    if (int(mLive.size()) >= mMaxLive) {
        despawn(phys, r, mLive.front());
        mLive.erase(mLive.begin());
    }

    fwd = glm::normalize(fwd);
    glm::vec3 spawnPos = eye + fwd * 0.5f;

    // Jolt capsule long axis is +Y; rotate +Y to fwd
    glm::quat orient = rotateFromTo(glm::vec3(0, 1, 0), fwd);

    eng::BodyDesc d;
    d.kind          = eng::ShapeKind::Capsule;
    d.radius        = 0.03f;
    d.halfHeight    = 0.22f;
    d.position      = spawnPos;
    d.orientation   = orient;
    d.layer         = eng::BodyLayer::Projectile;
    d.dynamic       = true;
    d.continuousCast = true;
    d.mass          = 0.1f;
    d.friction      = 0.8f;
    d.restitution   = 0.0f;

    eng::BodyHandle body = phys.createBody(d);
    if (!body.valid()) return;

    // impulse = mass * targetSpeed (60 m/s)
    phys.applyImpulse(body, fwd * (0.1f * 60.0f), spawnPos);

    eng::NodeHandle node = r.createNode(eng::kRootNode, spawnPos);
    r.setOrientation(node, orient);
    r.attachMesh(node, mArrowMesh, "Game/ProtoArrow", false);

    mLive.push_back({ body, node, Kind::Arrow, 10.0f, false });
}

void ProjectileSystem::fireBolt(eng::Physics& phys, eng::Renderer& r,
                                 glm::vec3 eye, glm::vec3 fwd) {
    if (int(mLive.size()) >= mMaxLive) {
        despawn(phys, r, mLive.front());
        mLive.erase(mLive.begin());
    }

    fwd = glm::normalize(fwd);
    glm::vec3 spawnPos = eye + fwd * 0.5f;

    eng::BodyDesc d;
    d.kind          = eng::ShapeKind::Sphere;
    d.radius        = 0.08f;
    d.position      = spawnPos;
    d.layer         = eng::BodyLayer::Projectile;
    d.dynamic       = true;
    d.continuousCast = true;
    d.mass          = 0.2f;
    d.friction      = 0.2f;
    d.restitution   = 0.1f;

    eng::BodyHandle body = phys.createBody(d);
    if (!body.valid()) return;

    // 25 m/s
    phys.applyImpulse(body, fwd * (0.2f * 25.0f), spawnPos);

    eng::NodeHandle node = r.createNode(eng::kRootNode, spawnPos);
    r.attachMesh(node, mBoltMesh, "Game/ProtoBolt", false);

    mLive.push_back({ body, node, Kind::Bolt, 8.0f, false });
}

// ---- contact seam ----

void ProjectileSystem::onHit(eng::Physics& phys, const eng::HitEvent& e) {
    for (auto& p : mLive) {
        if (p.body == e.self || p.body == e.other) {
            if (p.kind == Kind::Arrow && !p.stuck) {
                p.stuck = true;
                p.ttl   = 20.0f; // stuck arrows linger longer
                phys.setBodyKinematic(p.body, true);
            } else if (p.kind == Kind::Bolt && !p.stuck) {
                p.stuck = true;
                p.ttl   = 0.0f; // despawn next fixedUpdate
            }
            return;
        }
    }
}

// ---- fixed update ----

void ProjectileSystem::despawn(eng::Physics& phys, eng::Renderer& r,
                                Projectile& p) {
    phys.removeBody(p.body);
    r.setNodeVisible(p.node, false);
}

void ProjectileSystem::fixedUpdate(eng::Physics& phys, eng::Renderer& r,
                                    float dt) {
    for (auto& p : mLive) {
        if (!p.stuck)
            p.ttl -= dt;
        else if (p.kind == Kind::Arrow)
            p.ttl -= dt; // stuck arrows still count down to eventual cleanup
        else
            p.ttl -= dt; // bolts arrive here with ttl=0 already
    }

    // Remove expired projectiles (sweep from back to preserve indices)
    for (int i = int(mLive.size()) - 1; i >= 0; --i) {
        if (mLive[i].ttl <= 0.0f) {
            despawn(phys, r, mLive[i]);
            mLive.erase(mLive.begin() + i);
        }
    }
}

// ---- sync render ----

void ProjectileSystem::syncRender(eng::Physics& phys, eng::Renderer& r) {
    for (auto& p : mLive) {
        glm::vec3 pos;
        glm::quat rot;
        phys.getRenderTransform(p.body, pos, rot);
        r.setPosition(p.node, pos);
        r.setOrientation(p.node, rot);
    }
}

// ---- clear ----

void ProjectileSystem::clear(eng::Physics& phys, eng::Renderer& r) {
    for (auto& p : mLive)
        despawn(phys, r, p);
    mLive.clear();
}
