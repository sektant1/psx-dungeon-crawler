#include "WeaponDelivery.h"

#include "GameCollision.h"

#include <eng/Primitive.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace game {
namespace {

// The same rotation ProjectileSystem uses to point a +Y-authored primitive down
// a direction. Duplicated rather than shared because it is six lines of quat
// algebra, and the alternative was a "MathBits.h" nobody would find.
glm::quat rotateFromTo(glm::vec3 from, glm::vec3 to)
{
    from = glm::normalize(from);
    to = glm::normalize(to);
    const float dot = glm::dot(from, to);
    if (dot > 0.9999f)
        return glm::quat(1, 0, 0, 0);
    if (dot < -0.9999f) {
        const glm::vec3 axis = glm::normalize(
            std::fabs(from.x) < 0.9f ? glm::cross(from, glm::vec3(1, 0, 0))
                                     : glm::cross(from, glm::vec3(0, 1, 0)));
        return glm::angleAxis(glm::pi<float>(), axis);
    }
    const glm::vec3 axis = glm::cross(from, to);
    return glm::normalize(glm::quat(1.0f + dot, axis.x, axis.y, axis.z));
}

glm::vec3 muzzlePosition(glm::vec3 eye, glm::vec3 forward, glm::vec3 offset)
{
    forward = glm::normalize(forward);
    glm::vec3 right = glm::cross(forward, glm::vec3(0, 1, 0));
    if (glm::dot(right, right) < 0.0001f)
        right = glm::vec3(1, 0, 0);
    else
        right = glm::normalize(right);
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    return eye + right * offset.x + up * offset.y + forward * offset.z;
}

} // namespace

eng::MeshHandle WeaponDeliverySystem::beamMesh(eng::Renderer& renderer)
{
    if (!mBeamMesh.valid()) {
        // A unit box: y-scaled to the trace length at spawn, so one mesh serves
        // every beam at every range.
        eng::PrimitiveMeshDesc desc;
        desc.kind = eng::PrimitiveKind::Box;
        desc.size = glm::vec3(1.0f);
        mBeamMesh = renderer.createPrimitiveMesh(desc);
    }
    return mBeamMesh;
}

void WeaponDeliverySystem::fire(eng::Physics& physics, eng::Renderer& renderer,
                                const PlayerWeaponDef& weapon,
                                glm::vec3 aimOrigin, glm::vec3 aimDirection,
                                std::optional<glm::vec3> muzzleOrigin)
{
    if (glm::dot(aimDirection, aimDirection) < 0.000001f)
        return;
    aimDirection = glm::normalize(aimDirection);
    const glm::vec3 muzzle = muzzleOrigin.value_or(
        muzzlePosition(aimOrigin, aimDirection, weapon.muzzleOffset));

    if (!weapon.muzzleEffect.empty())
        renderer.spawnParticles(weapon.muzzleEffect, muzzle);

    switch (weapon.fireMode) {
    case WeaponFireMode::Projectile:
        // Not ours: the caller routes projectiles to ProjectileSystem. Reaching
        // here means a dispatch bug, and doing nothing is the honest response --
        // a melee sweep for a bolt would be worse than a shot that does not fire.
        break;
    case WeaponFireMode::Melee:
        // Replace any swing in flight rather than queueing: the fire interval
        // already decides how often a swing may start, and a queue would let a
        // held button bank swings that land after the button is released.
        mSwing = Swing{};
        mSwing.live = true;
        mSwing.def = weapon.melee;
        mSwing.payload = weapon.payloadId;
        mSwing.windupLeft = weapon.melee.windup;
        mSwing.activeLeft = weapon.melee.active;
        mSwing.alreadyHit.reserve(std::size_t(weapon.melee.maxTargets));
        break;
    case WeaponFireMode::Hitscan:
        resolveHitscan(physics, renderer, weapon, aimOrigin, aimDirection,
                       muzzle);
        break;
    }
}

void WeaponDeliverySystem::resolveHitscan(eng::Physics& physics,
                                          eng::Renderer& renderer,
                                          const PlayerWeaponDef& weapon,
                                          glm::vec3 aimOrigin,
                                          glm::vec3 aimDirection,
                                          glm::vec3 muzzle)
{
    const WeaponHitscanDef& hitscan = weapon.hitscan;

    // The ray is cast from the EYE, not the muzzle: the crosshair is the
    // contract, and a ray from a muzzle offset to the right of the eye clips
    // the doorframe the player is peeking around. The beam is then drawn from
    // the muzzle to wherever that ray landed, which is what sells the shot as
    // leaving the weapon without letting the weapon decide what it hit.
    eng::RayHit hit;
    const bool struck = physics.rayCast(aimOrigin, aimDirection, hitscan.range,
                                        hit, layer::kSolid);
    const glm::vec3 end =
        struck ? hit.point : aimOrigin + aimDirection * hitscan.range;

    if (!hitscan.beamMaterial.empty() && hitscan.beamSeconds > 0.0f) {
        const glm::vec3 span = end - muzzle;
        const float length = glm::length(span);
        if (length > 0.001f) {
            const eng::NodeHandle node =
                renderer.createNode(eng::kRootNode, (muzzle + end) * 0.5f);
            renderer.setOrientation(
                node, rotateFromTo(glm::vec3(0, 1, 0), span / length));
            renderer.setScale(
                node, glm::vec3(hitscan.beamWidth, length, hitscan.beamWidth));
            // renderOnTop stays false: a hitscan trace is a world object and
            // must be occluded by the pillar it passes behind. Only the
            // viewmodel is exempt from the depth buffer.
            renderer.attachMesh(node, beamMesh(renderer), hitscan.beamMaterial,
                                /*castShadows=*/false);
            mBeams.push_back({node, hitscan.beamSeconds});
        }
    }

    if (!struck)
        return;
    if (!hitscan.impactEffect.empty())
        renderer.spawnParticles(hitscan.impactEffect, end);
    if (hit.body.valid() && hitscan.impulse > 0.0f)
        physics.applyImpulse(hit.body, aimDirection * hitscan.impulse, end);
    if (mOnImpact)
        mOnImpact(hit.body, weapon.payloadId, aimDirection, end);
}

void WeaponDeliverySystem::stepSwing(eng::Physics& physics,
                                     eng::Renderer& renderer, glm::vec3 eye,
                                     glm::vec3 forward, float dt)
{
    if (!mSwing.live)
        return;
    if (mSwing.windupLeft > 0.0f) {
        mSwing.windupLeft = std::max(0.0f, mSwing.windupLeft - dt);
        return;
    }
    if (mSwing.activeLeft <= 0.0f) {
        mSwing.live = false;
        return;
    }
    mSwing.activeLeft = std::max(0.0f, mSwing.activeLeft - dt);

    if (glm::dot(forward, forward) < 0.000001f)
        return;
    forward = glm::normalize(forward);

    eng::BodyDesc sweep;
    sweep.kind = eng::ShapeKind::Sphere;
    sweep.radius = mSwing.def.radius;
    // Starting slightly ahead of the eye keeps the sweep out of the player's
    // own capsule; ending at `reach` is the number a designer tunes.
    const glm::vec3 from = eye + forward * 0.3f;
    const glm::vec3 to = eye + forward * mSwing.def.reach;

    std::vector<eng::ShapeHit> hits;
    physics.shapeCast(sweep, from, to, hits, layer::kHittable);
    for (const eng::ShapeHit& hit : hits) {
        if (int(mSwing.alreadyHit.size()) >= mSwing.def.maxTargets)
            break;
        // Once per body per swing. Without this the sweep re-hits the same
        // barrel on every fixed step of the active window, which is a weapon
        // that does `active / fixedDt` times its authored damage.
        if (std::find(mSwing.alreadyHit.begin(), mSwing.alreadyHit.end(),
                      hit.body.id) != mSwing.alreadyHit.end())
            continue;
        mSwing.alreadyHit.push_back(hit.body.id);

        if (mSwing.def.impulse > 0.0f)
            physics.applyImpulse(hit.body, forward * mSwing.def.impulse,
                                 hit.point);
        if (!mSwing.def.impactEffect.empty())
            renderer.spawnParticles(mSwing.def.impactEffect, hit.point);
        if (mOnImpact)
            mOnImpact(hit.body, mSwing.payload, forward, hit.point);
    }

    if (mSwing.activeLeft <= 0.0f)
        mSwing.live = false;
}

void WeaponDeliverySystem::fixedUpdate(eng::Physics& physics,
                                       eng::Renderer& renderer, glm::vec3 eye,
                                       glm::vec3 forward, float dt)
{
    stepSwing(physics, renderer, eye, forward, dt);

    for (std::size_t i = mBeams.size(); i-- > 0;) {
        mBeams[i].ttl -= dt;
        if (mBeams[i].ttl > 0.0f)
            continue;
        renderer.destroyNode(mBeams[i].node);
        mBeams.erase(mBeams.begin() + long(i));
    }
}

void WeaponDeliverySystem::clear(eng::Physics&, eng::Renderer& renderer)
{
    for (Beam& beam : mBeams)
        renderer.destroyNode(beam.node);
    mBeams.clear();
    mSwing = Swing{};
    // The beam mesh belongs to the renderer's scene and is destroyed with it by
    // clearScene; keeping the handle would hand out a dead mesh on the next
    // level. See the clearScene note in docs/fps-gameplay.md.
    mBeamMesh = {};
}

} // namespace game
