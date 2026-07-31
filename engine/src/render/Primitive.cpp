#include <eng/Primitive.h>

#include <eng/Log.h>
#include <eng/Renderer.h>

#include <algorithm>
#include <cmath>

namespace eng {

// primitiveMeshGenerator and validPrimitiveMeshDesc used to live here. They
// moved into PrimitiveGeometry.cpp (eng_core) because the geometry builder
// calls both, and the geometry builder is a bottom-layer file while this one
// is not: eng_core linked on its own could not resolve them, which is what
// broke rhi_contract_tests. Nothing about them needs a renderer.

namespace {

// Jolt refuses a box whose half-extent is below its convex radius; a millimetre
// is thin enough that no collision response reads as a slab and thick enough
// that the solver keeps the shape.
constexpr float kMinHalfExtent = 0.001f;

bool finiteVec3(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool finiteQuat(glm::quat value)
{
    return std::isfinite(value.w) && std::isfinite(value.x) &&
           std::isfinite(value.y) && std::isfinite(value.z);
}

PrimitiveDesc composePrimitiveDesc(const NodeTransform& parent,
                                   const PrimitiveDesc& local)
{
    PrimitiveDesc world = local;
    const glm::quat parentOrientation = glm::normalize(parent.orientation);
    world.parent = kRootNode;
    world.position =
        parent.position + parentOrientation * (parent.scale * local.position);
    world.orientation =
        glm::normalize(parentOrientation * glm::normalize(local.orientation));
    world.scale = parent.scale * local.scale;
    return world;
}

} // namespace

bool validPrimitiveDesc(const PrimitiveDesc& desc)
{
    if (!validPrimitiveMeshDesc(desc.mesh) || !desc.parent.valid() ||
        desc.material.empty() || !finiteVec3(desc.position) ||
        !finiteQuat(desc.orientation) || !finiteVec3(desc.scale) ||
        glm::any(glm::equal(desc.scale, glm::vec3(0.0f))) ||
        !std::isfinite(desc.mass) || desc.mass <= 0.0f ||
        !std::isfinite(desc.friction) || desc.friction < 0.0f ||
        !std::isfinite(desc.restitution) || desc.restitution < 0.0f)
        return false;

    const float orientationLength2 =
        glm::dot(desc.orientation, desc.orientation);
    return std::isfinite(orientationLength2) && orientationLength2 > 0.000001f;
}

std::optional<ResolvedPrimitiveCollider>
resolvePrimitiveCollider(const PrimitiveDesc& desc)
{
    if (!validPrimitiveDesc(desc))
        return std::nullopt;

    ResolvedPrimitiveCollider resolved;
    resolved.collision = desc.collision;
    resolved.body.position = desc.position;
    resolved.body.orientation = glm::normalize(desc.orientation);
    resolved.body.layer = desc.bodyLayer;
    resolved.body.dynamic = desc.dynamic;
    resolved.body.sensor = desc.sensor;
    resolved.body.mass = desc.mass;
    resolved.body.friction = desc.friction;
    resolved.body.restitution = desc.restitution;
    if (!desc.collision)
        return resolved;

    const glm::vec3 scale = glm::abs(desc.scale);
    const float radialScale = std::max(scale.x, scale.z);
    switch (desc.mesh.kind) {
    case PrimitiveKind::Box:
    case PrimitiveKind::BeveledBox:
        resolved.body.kind = ShapeKind::Box;
        resolved.body.halfExtents = desc.mesh.size * scale * 0.5f;
        break;
    case PrimitiveKind::Plane:
        resolved.body.kind = ShapeKind::Box;
        resolved.body.halfExtents = {
            desc.mesh.size.x * scale.x * 0.5f,
            // A flat plane renders as a single quad, but a box shape with a
            // zero half-extent is degenerate to the physics backend, so a
            // collidable one gets the thinnest slab the solver still accepts.
            std::max(desc.mesh.thickness * scale.y * 0.5f, kMinHalfExtent),
            desc.mesh.size.z * scale.z * 0.5f,
        };
        break;
    case PrimitiveKind::Sphere:
        resolved.body.kind = ShapeKind::Sphere;
        resolved.body.radius =
            desc.mesh.radius * std::max({scale.x, scale.y, scale.z});
        break;
    case PrimitiveKind::Capsule:
        resolved.body.kind = ShapeKind::Capsule;
        resolved.body.radius =
            desc.mesh.radius * std::max({scale.x, scale.y, scale.z});
        resolved.body.halfHeight = desc.mesh.height * scale.y * 0.5f;
        break;
    case PrimitiveKind::Cylinder:
        resolved.body.kind = ShapeKind::Cylinder;
        resolved.body.radius = desc.mesh.radius * radialScale;
        resolved.body.halfHeight = desc.mesh.height * scale.y * 0.5f;
        break;
    case PrimitiveKind::Cone:
        // Physics exposes no cone shape. A cylinder with the same radius and
        // height is a conservative approximation that cannot tunnel through
        // geometry visible inside the cone's rendered silhouette.
        resolved.body.kind = ShapeKind::Cylinder;
        resolved.body.radius = desc.mesh.radius * radialScale;
        resolved.body.halfHeight = desc.mesh.height * scale.y * 0.5f;
        break;
    case PrimitiveKind::Disc:
        resolved.body.kind = ShapeKind::Cylinder;
        resolved.body.radius = desc.mesh.radius * radialScale;
        resolved.body.halfHeight = desc.mesh.thickness * scale.y * 0.5f;
        break;
    }

    return resolved;
}

std::optional<ResolvedPrimitiveCollider>
resolvePrimitiveCollider(const PrimitiveDesc& local,
                         const NodeTransform& parent)
{
    if (!validPrimitiveDesc(local) || !finiteVec3(parent.position) ||
        !finiteQuat(parent.orientation) || !finiteVec3(parent.scale))
        return std::nullopt;
    const float parentOrientationLength2 =
        glm::dot(parent.orientation, parent.orientation);
    if (!std::isfinite(parentOrientationLength2) ||
        parentOrientationLength2 <= 0.000001f)
        return std::nullopt;
    return resolvePrimitiveCollider(composePrimitiveDesc(parent, local));
}

PrimitiveInstance spawnPrimitive(Renderer& renderer, Physics& physics,
                                 const PrimitiveDesc& desc)
{
    if (!validPrimitiveDesc(desc)) {
        log::error("Primitive: invalid descriptor");
        return {};
    }

    NodeTransform parentTransform;
    if (!renderer.nodeWorldTransform(desc.parent, parentTransform)) {
        log::error("Primitive: invalid parent node");
        return {};
    }
    const auto resolved = resolvePrimitiveCollider(desc, parentTransform);
    if (!resolved) {
        log::error("Primitive: collider resolution failed");
        return {};
    }

    const MeshHandle mesh = renderer.createPrimitiveMesh(desc.mesh);
    if (!mesh.valid())
        return {};
    const NodeHandle node = renderer.createNode(desc.parent, desc.position);
    if (!node.valid()) {
        renderer.releaseMesh(mesh);
        return {};
    }
    renderer.setOrientation(node, glm::normalize(desc.orientation));
    renderer.setScale(node, desc.scale);
    renderer.attachMesh(node, mesh, desc.material, desc.castShadows);
    if (desc.enchantment)
        renderer.setNodeEnchantment(node, *desc.enchantment);

    BodyHandle body;
    if (resolved->createsBody())
        body = physics.createBody(resolved->body);
    if (resolved->createsBody() && !body.valid()) {
        renderer.destroyNode(node);
        renderer.releaseMesh(mesh);
        return {};
    }
    return {node, mesh, body};
}

void destroyPrimitive(Renderer& renderer, Physics& physics,
                      PrimitiveInstance& instance)
{
    detail::releasePrimitiveOwnership(
        instance, [&](BodyHandle body) { physics.removeBody(body); },
        [&](NodeHandle node) { renderer.destroyNode(node); },
        [&](MeshHandle mesh) { renderer.releaseMesh(mesh); });
}

} // namespace eng
