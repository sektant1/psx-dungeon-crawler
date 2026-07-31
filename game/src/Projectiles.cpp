#include "Projectiles.h"

#include "GameCollision.h"

#include <eng/Primitive.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace {

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

eng::PrimitiveKind primitiveKind(game::WeaponPrimitive primitive)
{
    switch (primitive) {
        case game::WeaponPrimitive::Box: return eng::PrimitiveKind::Box;
        case game::WeaponPrimitive::BeveledBox:
            return eng::PrimitiveKind::BeveledBox;
        case game::WeaponPrimitive::Sphere: return eng::PrimitiveKind::Sphere;
        case game::WeaponPrimitive::Capsule: return eng::PrimitiveKind::Capsule;
        case game::WeaponPrimitive::Cylinder:
            return eng::PrimitiveKind::Cylinder;
        case game::WeaponPrimitive::Cone: return eng::PrimitiveKind::Cone;
        case game::WeaponPrimitive::Disc: return eng::PrimitiveKind::Disc;
    }
    return eng::PrimitiveKind::Sphere;
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

eng::MeshHandle ProjectileSystem::meshFor(
    eng::Renderer& renderer, const game::PlayerWeaponDef& definition)
{
    const auto existing = mMeshes.find(definition.id);
    if (existing != mMeshes.end())
        return existing->second;
    eng::PrimitiveMeshDesc mesh;
    mesh.kind = primitiveKind(definition.projectile.primitive);
    mesh.rings = 8;
    mesh.segments = 10;
    mesh.bevel = 0.08f;
    const eng::MeshHandle handle = renderer.createPrimitiveMesh(mesh);
    mMeshes.emplace(definition.id, handle);
    return handle;
}

void ProjectileSystem::fire(eng::Physics& physics, eng::Renderer& renderer,
                            const game::PlayerWeaponDef& weapon,
                            glm::vec3 aimOrigin, glm::vec3 aimDirection)
{
    if (glm::dot(aimDirection, aimDirection) < 0.000001f)
        return;
    aimDirection = glm::normalize(aimDirection);
    const glm::vec3 muzzle = muzzlePosition(aimOrigin, aimDirection,
                                            weapon.muzzleOffset);

    eng::RayHit targetHit;
    const bool aimedAtBody = physics.rayCast(
        aimOrigin, aimDirection, weapon.projectile.aimRange, targetHit,
        game::layer::kSolid);
    const glm::vec3 target = aimedAtBody
                                 ? targetHit.point
                                 : aimOrigin + aimDirection *
                                                   weapon.projectile.aimRange;
    glm::vec3 converged = target - muzzle;
    if (glm::dot(converged, converged) < 0.04f)
        converged = aimDirection;

    if (!weapon.muzzleEffect.empty())
        renderer.spawnParticles(weapon.muzzleEffect, muzzle);

    const std::vector<glm::vec3> directions = game::projectileDirections(
        converged, weapon.projectileCount, weapon.spreadDegrees);
    for (const glm::vec3 direction : directions) {
        if (int(mLive.size()) >= mMaxLive) {
            despawn(physics, renderer, mLive.front());
            mLive.erase(mLive.begin());
        }

        eng::BodyDesc bodyDesc;
        bodyDesc.kind = eng::ShapeKind::Sphere;
        bodyDesc.radius = weapon.projectile.radius;
        bodyDesc.position = muzzle;
        bodyDesc.orientation = rotateFromTo(glm::vec3(0, 1, 0), direction);
        bodyDesc.layer = game::layer::Projectile;
        bodyDesc.dynamic = true;
        bodyDesc.continuousCast = true;
        bodyDesc.mass = weapon.projectile.mass;
        bodyDesc.gravityFactor = weapon.projectile.gravityFactor;
        bodyDesc.friction = 0.0f;
        bodyDesc.restitution = 0.0f;
        const eng::BodyHandle body = physics.createBody(bodyDesc);
        if (!body.valid())
            continue;
        physics.applyImpulse(body,
                             direction * weapon.projectile.mass *
                                 weapon.projectile.speed,
                             muzzle);

        const eng::NodeHandle node = renderer.createNode(eng::kRootNode, muzzle);
        renderer.setOrientation(node, bodyDesc.orientation);
        renderer.setScale(node, weapon.projectile.visualScale);
        renderer.attachMesh(node, meshFor(renderer, weapon),
                            weapon.projectile.material, false);
        eng::ParticlesHandle trail;
        if (!weapon.projectile.trailEffect.empty())
            trail = renderer.spawnParticles(weapon.projectile.trailEffect, node);
        mLive.push_back({body, node, trail, weapon.payloadId,
                         weapon.projectile.impactEffect, direction,
                         weapon.projectile.lifetime, false});
    }
}

void ProjectileSystem::onHit(eng::Physics& physics, eng::Renderer& renderer,
                             const eng::HitEvent& event)
{
    for (Projectile& projectile : mLive) {
        const bool self = projectile.body == event.self;
        if ((!self && projectile.body != event.other) || projectile.impacted)
            continue;
        projectile.impacted = true;
        physics.setBodyKinematic(projectile.body, true);
        if (!projectile.impactEffect.empty())
            renderer.spawnParticles(projectile.impactEffect, event.point);
        if (mOnImpact)
            mOnImpact(self ? event.other : event.self, projectile.payload,
                      projectile.direction, event.point);
        return;
    }
}

void ProjectileSystem::despawn(eng::Physics& physics, eng::Renderer& renderer,
                               Projectile& projectile)
{
    if (projectile.trail.valid())
        renderer.despawnParticles(projectile.trail);
    physics.removeBody(projectile.body);
    renderer.destroyNode(projectile.node);
}

void ProjectileSystem::fixedUpdate(eng::Physics& physics,
                                   eng::Renderer& renderer, float dt)
{
    for (Projectile& projectile : mLive)
        projectile.ttl -= dt;
    for (int i = int(mLive.size()) - 1; i >= 0; --i) {
        if (mLive[std::size_t(i)].impacted ||
            mLive[std::size_t(i)].ttl <= 0.0f) {
            despawn(physics, renderer, mLive[std::size_t(i)]);
            mLive.erase(mLive.begin() + i);
        }
    }
}

void ProjectileSystem::syncRender(eng::Physics& physics,
                                  eng::Renderer& renderer)
{
    for (Projectile& projectile : mLive) {
        glm::vec3 position;
        glm::quat orientation;
        physics.getRenderTransform(projectile.body, position, orientation);
        renderer.setPosition(projectile.node, position);
        renderer.setOrientation(projectile.node, orientation);
    }
}

void ProjectileSystem::clear(eng::Physics& physics, eng::Renderer& renderer)
{
    for (Projectile& projectile : mLive)
        despawn(physics, renderer, projectile);
    mLive.clear();
    for (const auto& [id, mesh] : mMeshes)
        renderer.releaseMesh(mesh);
    mMeshes.clear();
}
