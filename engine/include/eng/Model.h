#pragma once

#include <eng/Physics.h>
#include <eng/Renderer.h>
#include <eng/render/PrototypeAssets.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace eng {

enum class ColliderMode { None, AutoBox, AutoSphere, StaticMesh };

struct ModelDesc {
    std::string meshPath;
    std::string material;
    std::string fallbackMaterial = prototype::kSurfaceMaterial;
    std::vector<std::string> submeshMaterials;
    ModelImportOptions import;
    NodeHandle parent = kRootNode;
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    bool castShadows = false;
    bool renderOnTop = false;
    std::optional<EnchantmentDesc> enchantment;
    ColliderMode collider = ColliderMode::None;
    std::optional<glm::vec3> colliderHalfExtents;
    std::optional<float> colliderRadius;
    CollisionLayer bodyLayer = 0;
    bool dynamic = false;
    bool sensor = false;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
};

struct ModelInstance {
    NodeHandle node;
    MeshHandle mesh;
    BodyHandle body;

    bool valid() const { return node.valid() && mesh.valid(); }
};

// Pure result used by spawnModel and focused descriptor tests. StaticMesh
// carries its mode here but uses Renderer-owned triangle data at runtime.
struct ResolvedModelCollider {
    ColliderMode mode = ColliderMode::None;
    BodyDesc body;

    bool createsBody() const { return mode != ColliderMode::None; }
};

namespace detail {

inline bool finiteVec3(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

inline bool finiteQuat(glm::quat value)
{
    return std::isfinite(value.w) && std::isfinite(value.x) &&
           std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace detail

inline bool validModelDesc(const ModelDesc& desc)
{
    if (desc.meshPath.empty() || desc.fallbackMaterial.empty() ||
        !desc.parent.valid() || !detail::finiteVec3(desc.position) ||
        !detail::finiteVec3(desc.scale) ||
        !detail::finiteQuat(desc.orientation))
        return false;

    const float orientationLength2 =
        glm::dot(desc.orientation, desc.orientation);
    if (!std::isfinite(orientationLength2) ||
        orientationLength2 <= 0.000001f)
        return false;

    if (!std::isfinite(desc.mass) || desc.mass <= 0.0f ||
        !std::isfinite(desc.friction) || desc.friction < 0.0f ||
        !std::isfinite(desc.restitution) || desc.restitution < 0.0f)
        return false;

    if (desc.colliderHalfExtents &&
        (!detail::finiteVec3(*desc.colliderHalfExtents) ||
         glm::any(glm::lessThanEqual(*desc.colliderHalfExtents,
                                     glm::vec3(0.0f)))))
        return false;
    if (desc.colliderRadius &&
        (!std::isfinite(*desc.colliderRadius) ||
         *desc.colliderRadius <= 0.0f))
        return false;

    switch (desc.collider) {
    case ColliderMode::None:
    case ColliderMode::AutoBox:
    case ColliderMode::AutoSphere:
    case ColliderMode::StaticMesh:
        return true;
    }
    return false;
}

// Indexed overrides win; the legacy one-material request is the default for
// every remaining submesh. Absent/blank requests resolve to model fallback.
inline std::string modelRequestedMaterialForSubmesh(
    const ModelDesc& desc, size_t submeshIndex)
{
    if (submeshIndex < desc.submeshMaterials.size())
        return desc.submeshMaterials[submeshIndex];
    return desc.material;
}

inline ResolvedModelMaterial resolveModelMaterialForSubmesh(
    const ModelDesc& desc, size_t submeshIndex, bool requestedAvailable)
{
    ResolvedModelMaterial result;
    result.requested =
        modelRequestedMaterialForSubmesh(desc, submeshIndex);
    result.usedFallback =
        result.requested.empty() || !requestedAvailable;
    result.material =
        result.usedFallback ? desc.fallbackMaterial : result.requested;
    return result;
}

template <typename MaterialLookup>
inline ResolvedModelMaterial resolveModelMaterialForSubmesh(
    const ModelDesc& desc, size_t submeshIndex,
    MaterialLookup&& materialAvailable)
{
    const std::string requested =
        modelRequestedMaterialForSubmesh(desc, submeshIndex);
    return resolveModelMaterialForSubmesh(
        desc, submeshIndex,
        !requested.empty() && materialAvailable(requested));
}

inline NodeTransform composeNodeTransform(const NodeTransform& parent,
                                          const NodeTransform& local)
{
    NodeTransform world;
    const glm::quat parentOrientation =
        glm::normalize(parent.orientation);
    const glm::quat localOrientation =
        glm::normalize(local.orientation);
    world.position =
        parent.position +
        parentOrientation * (parent.scale * local.position);
    world.orientation =
        glm::normalize(parentOrientation * localOrientation);
    world.scale = parent.scale * local.scale;
    return world;
}

inline ModelDesc composeModelDesc(const NodeTransform& parent,
                                  const ModelDesc& local)
{
    const NodeTransform world = composeNodeTransform(
        parent, {local.position, local.orientation, local.scale});
    ModelDesc result = local;
    result.parent = kRootNode;
    result.position = world.position;
    result.orientation = world.orientation;
    result.scale = world.scale;
    return result;
}

inline std::optional<ResolvedModelCollider>
resolveModelCollider(const ModelDesc& desc, const MeshBounds& bounds)
{
    if (!validModelDesc(desc) ||
        (desc.collider == ColliderMode::StaticMesh && desc.dynamic))
        return std::nullopt;

    ResolvedModelCollider resolved;
    resolved.mode = desc.collider;
    resolved.body.position = desc.position;
    resolved.body.orientation = glm::normalize(desc.orientation);
    resolved.body.layer = desc.bodyLayer;
    resolved.body.dynamic = desc.dynamic;
    resolved.body.sensor = desc.sensor;
    resolved.body.mass = desc.mass;
    resolved.body.friction = desc.friction;
    resolved.body.restitution = desc.restitution;

    if (desc.collider == ColliderMode::None ||
        desc.collider == ColliderMode::StaticMesh)
        return resolved;

    if (!detail::finiteVec3(bounds.min) ||
        !detail::finiteVec3(bounds.max) ||
        glm::any(glm::greaterThan(bounds.min, bounds.max)))
        return std::nullopt;

    const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
    const glm::vec3 localHalfExtents = (bounds.max - bounds.min) * 0.5f;
    const glm::vec3 scaledCenter = center * desc.scale;
    resolved.body.position += resolved.body.orientation * scaledCenter;
    if (!detail::finiteVec3(resolved.body.position))
        return std::nullopt;

    const glm::vec3 scaledHalfExtents =
        localHalfExtents * glm::abs(desc.scale);
    if (!detail::finiteVec3(scaledHalfExtents))
        return std::nullopt;

    if (desc.collider == ColliderMode::AutoBox) {
        resolved.body.kind = ShapeKind::Box;
        resolved.body.halfExtents =
            desc.colliderHalfExtents.value_or(scaledHalfExtents);
        if (glm::any(glm::lessThanEqual(resolved.body.halfExtents,
                                        glm::vec3(0.0f))))
            return std::nullopt;
    } else {
        resolved.body.kind = ShapeKind::Sphere;
        resolved.body.radius = desc.colliderRadius.value_or(
            std::max({scaledHalfExtents.x, scaledHalfExtents.y,
                      scaledHalfExtents.z}));
        if (!std::isfinite(resolved.body.radius) ||
            resolved.body.radius <= 0.0f)
            return std::nullopt;
    }

    return resolved;
}

inline std::optional<ResolvedModelCollider>
resolveModelCollider(const ModelDesc& local, const MeshBounds& bounds,
                     const NodeTransform& parent)
{
    return resolveModelCollider(composeModelDesc(parent, local), bounds);
}

ModelInstance spawnModel(Renderer&, Physics&, const ModelDesc&);
void destroyModel(Renderer&, Physics&, ModelInstance&);

} // namespace eng
